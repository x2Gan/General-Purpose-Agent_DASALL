#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "LLMBackedSummarizer.h"
#include "MockLLMManager.h"
#include "ObservabilityLiveComposition.h"
#include "audit/AuditService.h"
#include "error/MemoryError.h"
#include "logging/FileLogReader.h"
#include "logging/LogQueryService.h"
#include "logging/LoggingFacade.h"
#include "metrics/MetricsFacade.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/ITracerProvider.h"
#include "tracing/TraceTypes.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::mocks::MockLLMManager;

[[nodiscard]] std::string make_hex_id(std::uint64_t value, std::size_t width) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string hex(width, '0');
  for (std::size_t index = 0; index < width; ++index) {
    hex[width - 1U - index] = kDigits[value & 0x0FU];
    value >>= 4U;
  }
  return hex;
}

class RecordingSpan final : public dasall::infra::tracing::ISpan {
 public:
  explicit RecordingSpan(dasall::infra::tracing::TraceContext context)
      : context_(std::move(context)) {}

  void set_attribute(
      std::string_view key,
      const dasall::infra::tracing::TraceAttributeValue& value) override {
    attributes[std::string(key)] = value;
  }

  void add_event(
      std::string_view,
      const dasall::infra::tracing::TraceAttributeMap&) override {}

  void set_status(
      dasall::infra::tracing::SpanStatusCode code,
      std::string_view message) override {
    status_code = code;
    status_message = std::string(message);
  }

  dasall::infra::tracing::SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    return dasall::infra::tracing::SpanEndResult{
        .end_ts_unix_ms = end_ts_unix_ms,
        .status_code = status_code,
        .status_message = status_message,
        .dropped_attr_count = 0U,
    };
  }

  dasall::infra::tracing::TraceContext get_context() const override {
    return context_;
  }

  dasall::infra::tracing::TraceContext context_;
  dasall::infra::tracing::TraceAttributeMap attributes;
  dasall::infra::tracing::SpanStatusCode status_code =
      dasall::infra::tracing::SpanStatusCode::Unset;
  std::string status_message;
};

struct StartedSpanRecord {
  dasall::infra::tracing::SpanDescriptor descriptor;
  std::shared_ptr<RecordingSpan> span;
};

class RecordingTracer final : public dasall::infra::tracing::ITracer {
 public:
  std::shared_ptr<dasall::infra::tracing::ISpan> start_span(
      const dasall::infra::tracing::SpanDescriptor& descriptor,
      const dasall::infra::tracing::TraceContext*) override {
    auto span = std::make_shared<RecordingSpan>(
        dasall::infra::tracing::TraceContext{
        .trace_id = make_hex_id(
          ++trace_seed_, dasall::infra::tracing::kTraceIdHexLength),
        .span_id = make_hex_id(
          ++span_seed_, dasall::infra::tracing::kSpanIdHexLength),
            .trace_flags = 0x01U,
            .trace_state = std::string(),
            .parent_span_id = std::string(),
            .state = dasall::infra::tracing::TraceContextState::Active,
            .is_remote = false,
        });
    started_spans.push_back(StartedSpanRecord{
        .descriptor = descriptor,
        .span = span,
    });
    return span;
  }

  void with_active_span(
      const std::shared_ptr<dasall::infra::tracing::ISpan>&,
      const dasall::infra::tracing::ActiveSpanCallback& fn) override {
    if (fn) {
      fn();
    }
  }

  dasall::infra::tracing::TraceContext current_context() const override {
    return dasall::infra::tracing::TraceContext::noop();
  }

  std::vector<StartedSpanRecord> started_spans;

 private:
  std::uint64_t trace_seed_ = 0U;
  std::uint64_t span_seed_ = 0U;
};

class RecordingTracerProvider final : public dasall::infra::tracing::ITracerProvider {
 public:
  explicit RecordingTracerProvider(std::shared_ptr<RecordingTracer> tracer)
      : tracer_(std::move(tracer)) {}

  dasall::infra::tracing::TraceOperationStatus init(
      const dasall::infra::tracing::TraceConfig&) override {
    return dasall::infra::tracing::TraceOperationStatus::success(
        "trace://memory/provider-init");
  }

  std::shared_ptr<dasall::infra::tracing::ITracer> get_tracer(
      const dasall::infra::tracing::TracerScope& scope) override {
    last_scope = scope;
    return tracer_;
  }

  dasall::infra::tracing::TraceOperationStatus force_flush(std::uint32_t) override {
    return dasall::infra::tracing::TraceOperationStatus::success(
        "trace://memory/provider-flush");
  }

  dasall::infra::tracing::TraceOperationStatus shutdown(std::uint32_t) override {
    return dasall::infra::tracing::TraceOperationStatus::success(
        "trace://memory/provider-shutdown");
  }

  dasall::infra::tracing::TracerScope last_scope{};

 private:
  std::shared_ptr<RecordingTracer> tracer_;
};

class ScopedTempDir {
 public:
  explicit ScopedTempDir(const std::string& stem)
      : path_(fs::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::steady_clock::now()
                                                 .time_since_epoch()
                                                 .count()))) {
    fs::create_directories(path_);
  }

  ~ScopedTempDir() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

void execute_sql(const fs::path& database_path, const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for memory production logging test");
  }

  char* error_message = nullptr;
  const int sqlite_status = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message = error_message == nullptr ? "failed to execute sqlite statement"
                                                         : error_message;
    sqlite3_free(error_message);
    sqlite3_close(connection);
    throw std::runtime_error(message);
  }

  sqlite3_close(connection);
}

void seed_old_quarantine_record(const fs::path& database_path,
                                const std::string& object_id) {
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.maintenance.quarantine_ttl_ms = 1000;

  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to open sqlite store for memory production logging test");
  }

  if (!store->quarantine_record("turn", object_id, "memory-production-logging").ok) {
    throw std::runtime_error("failed to seed quarantine record for memory production logging test");
  }
  store->close();

  execute_sql(database_path,
              "UPDATE quarantined_records SET created_at = 1 WHERE object_id = '" +
                  object_id + "'");
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void cleanup_database_artifacts(const fs::path& database_path) {
  (void)fs::remove(database_path);
  (void)fs::remove(database_path.string() + "-wal");
  (void)fs::remove(database_path.string() + "-shm");
}

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  std::string_view expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool contains_fragment(std::string_view value,
                                     std::string_view fragment) {
  return value.find(fragment) != std::string_view::npos;
}

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context() {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Allow,
          .reason_code = std::string("allow_diag_pull"),
          .matched_rule_ids = {std::string("memory-logging-rule")},
          .snapshot_id = std::string("policy-snapshot-memory-logging"),
          .generation = 7,
          .evidence_ref = std::string("policy://memory/logging/integration"),
          .warnings = {},
      },
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-memory-production-logging-query"),
          .session_id = std::string("session-memory-production-logging"),
          .trace_id = std::string("trace-memory-production-logging"),
          .task_id = std::string("task-memory-production-logging"),
          .parent_task_id = std::string("parent-memory-production-logging"),
          .lease_id = std::string("lease-memory-production-logging"),
      },
  };
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const fs::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.compression_trigger_turns = 2;
  config.context.compression_trigger_ratio = 0.5;
  config.vector.enabled = false;
  config.maintenance.quarantine_ttl_ms = 1000;
  return config;
}

[[nodiscard]] dasall::infra::ExportQuery make_audit_export_query() {
  return dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };
}

[[nodiscard]] const dasall::infra::AuditEvent* find_audit_event(
    const dasall::infra::ExportResult& export_result,
    std::string_view action) {
  for (auto it = export_result.records.rbegin(); it != export_result.records.rend(); ++it) {
    if (it->action == action) {
      return &*it;
    }
  }

  return nullptr;
}

[[nodiscard]] bool audit_event_has_side_effect(
    const dasall::infra::AuditEvent& event,
    std::string_view expected) {
  return std::any_of(event.side_effects.begin(),
                     event.side_effects.end(),
                     [&expected](const std::string& side_effect) {
                       return side_effect == expected;
                     });
}

[[nodiscard]] const StartedSpanRecord* find_started_span(
    const std::vector<StartedSpanRecord>& started_spans,
    std::string_view name) {
  for (auto it = started_spans.rbegin(); it != started_spans.rend(); ++it) {
    if (it->descriptor.name == name) {
      return &*it;
    }
  }

  return nullptr;
}

[[nodiscard]] const std::string* string_attr(
    const dasall::infra::tracing::TraceAttributeMap& attrs,
    std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  return attr != nullptr ? std::get_if<std::string>(attr) : nullptr;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& fact_text,
    std::uint32_t confidence_score,
    bool include_experience) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = 3000 + static_cast<std::int64_t>(confidence_score);
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = std::string{"summary-secret:"} + turn_id;
  request.summary_candidate->confirmed_facts = std::vector<std::string>{fact_text};

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = confidence_score;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "turn";
  request.fact_candidates.push_back(std::move(fact_candidate));

  if (include_experience) {
    dasall::memory::ExperienceCandidate experience_candidate;
    experience_candidate.experience.lesson_summary =
        "experience-secret: supersede relation should remain auditable";
    experience_candidate.experience.trigger_condition = "fact conflict detected";
    experience_candidate.experience.recommended_action =
        "record supersede relation and continue";
    experience_candidate.experience.created_at = 5000;
    experience_candidate.extraction_source = "reflection";
    request.experience_candidates.push_back(std::move(experience_candidate));
  }

  return request;
}

void test_memory_production_logging_persists_queryable_redacted_events() {
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::logging::FileLogReader;
  using dasall::infra::logging::FileLogReaderOptions;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LogQueryServiceOptions;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-memory-production-logging");
  const auto database_path = temp_dir.path() / "memory-production-logging.db";
  const auto runtime_log_path = temp_dir.path() / "logging" / "runtime.log";
  const auto artifact_root = temp_dir.path() / "query-artifacts";
  const std::string session_id = "session-memory-production-logging";
  const std::string trace_id = "trace-memory-production-logging";
  const std::string writeback_request_id = "req-memory-production-writeback";
  const std::string context_request_id = "req-memory-production-context";
  const std::string maintenance_request_id = "req-memory-production-maintenance";
  cleanup_database_artifacts(database_path);

  ObservabilityLiveCompositionOptions options;
  options.profile_id = "desktop_full";
  options.metrics_granularity = "full";
  options.trace_sample_ratio = 1.0;
  options.logging_state_root_override = temp_dir.path();
  const auto observability = compose_live_observability(options);
  assert_true(observability.ok(),
              std::string("memory production logging integration should compose live providers: ") +
                  observability.error);

  const auto logger =
      std::dynamic_pointer_cast<LoggingFacade>(observability.logger);
  assert_true(logger != nullptr,
              "memory production logging integration should keep the concrete logger inspectable");

  const auto config = make_sqlite_config(database_path);
    auto llm_manager = std::make_shared<MockLLMManager>();
    llm_manager->set_generate_handler(
      [](const dasall::llm::LLMGenerateRequest& request) {
      return MockLLMManager::make_structured_stage_result(
        request.stage,
        R"({"schema_version":"memory_summary.v1","request_id":"req-memory-production-logging-summary","summary_text":"LLM 生产摘要：logging path 已接线","decisions_made":["保留 summarizer strategy 审计字段"],"confirmed_facts":["memory logging integration 走 LLM summarizer"],"tool_outcomes":["memory_logging:test"]})",
        request.request.request_id);
      });
  auto manager = dasall::memory::create_memory_manager(
      config,
      dasall::memory::MemoryRuntimeDependencies{
          .logger = observability.logger,
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
        .summarizer_factory =
          [llm_manager](const dasall::memory::MemoryConfig&) {
          return std::make_unique<
            dasall::apps::runtime_support::LLMBackedSummarizer>(
            llm_manager);
          },
          .profile_id = "desktop_full",
      });
  assert_true(manager != nullptr,
              "memory production logging integration should create a manager with live sinks");

  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory production logging integration should initialize the sqlite-backed manager");

  auto writeback_request = make_request(
      session_id,
      "turn-memory-production-logging-001",
      "user_input=memory-secret-input",
      "agent_response=memory-secret-output token=memory-secret-token",
      "fact_text=memory-secret-fact",
      88,
      true);
  writeback_request.request_id = writeback_request_id;
  writeback_request.trace_id = trace_id;
  const auto writeback = manager->write_back(writeback_request);
  assert_true(!writeback.result_code.has_value(),
              "memory production logging integration should complete the writeback path");

  const auto context_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
          .request_id = context_request_id,
          .session_id = session_id,
          .trace_id = trace_id,
          .stage = "reasoning",
          .goal_summary = "goal_summary=memory-secret-goal",
          .constraints_summary = "constraints should remain owner local",
          .latest_observation_digest_summary = "latest_observation_digest_summary=memory-secret-digest",
          .visible_tools = {"shell", "cmake"},
          .token_budget_hint = 240,
          .latency_budget_ms = 150,
          .external_evidence = {"external_evidence=memory-secret-evidence"},
          .retrieval_evidence_refs = {dasall::contracts::RetrievalEvidenceRef{
              .evidence_ref = "retrieval-secret-ref",
              .source_ref = "knowledge://memory-production-logging",
              .source_kind = "knowledge",
              .summary_text = "retrieval-summary-should-not-log",
              .trust_level = "high",
              .freshness = "fresh",
              .anchor_locator = std::string("turn:memory-production-logging"),
          }},
      });
  assert_true(!context_result.result_code.has_value(),
              "memory production logging integration should complete the context assembly path");
          assert_true(logger->has_last_dispatched_event() &&
                  logger->last_dispatched_event().attrs.at("event_name") ==
                    "context.assembled" &&
                  logger->last_dispatched_event().attrs.at("compression_strategy") ==
                    "summarizer",
                "memory production logging integration should retain the summarizer compression strategy on the context event");

  seed_old_quarantine_record(database_path, "memory-production-logging-quarantine");
  const auto maintenance_report = manager->run_maintenance(
      dasall::memory::MaintenanceRequest{
          .run_checkpoint = false,
          .run_retention = false,
          .run_quarantine_cleanup = true,
          .run_vector_rebuild = false,
          .request_id = maintenance_request_id,
          .trace_id = trace_id,
      });
  assert_true(maintenance_report.quarantine_cleaned == 1,
              "memory production logging integration should complete the maintenance path");

  manager->shutdown();

  assert_true(logger->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "memory production logging integration should flush the logger before querying runtime.log");

  auto reader = std::make_shared<FileLogReader>(FileLogReaderOptions{
      .runtime_log_path = runtime_log_path,
      .include_rotation_family = true,
  });
  LogQueryService service(reader,
                          LogQueryServiceOptions{
                              .enable_diag_pull = true,
                              .artifact_namespace = "diag://infra/logging/query",
                              .artifact_root_dir = artifact_root,
                              .index_file_name = "query-index.jsonl",
                              .retention_policy = {.retention_days = 7, .max_artifact_count = 8},
                          },
                          []() { return static_cast<std::int64_t>(4102444800000); });

  const auto session_query_result = service.query(LogQueryRequest{
                                                      .query_id = std::string("memory-prod-logging-session"),
                                                      .selector_kind = LogQuerySelectorKind::SessionId,
                                                      .selector_value = session_id,
                                                      .start_ts_ms = 1,
                                                      .end_ts_ms = 4102444800000,
                                                      .max_records = 16,
                                                  },
                                                  make_access_context());
  const auto trace_query_result = service.query(LogQueryRequest{
                                                    .query_id = std::string("memory-prod-logging-trace"),
                                                    .selector_kind = LogQuerySelectorKind::TraceId,
                                                    .selector_value = trace_id,
                                                    .start_ts_ms = 1,
                                                    .end_ts_ms = 4102444800000,
                                                    .max_records = 16,
                                                },
                                                make_access_context());
  const auto request_query_result = service.query(LogQueryRequest{
                                                      .query_id = std::string("memory-prod-logging-request"),
                                                      .selector_kind = LogQuerySelectorKind::RequestId,
                                                      .selector_value = writeback_request_id,
                                                      .start_ts_ms = 1,
                                                      .end_ts_ms = 4102444800000,
                                                      .max_records = 16,
                                                  },
                                                  make_access_context());

  const auto runtime_log_text = read_text(runtime_log_path);
  const auto session_artifact_text = read_text(
      artifact_root / "memory-prod-logging-session-4102444800000.json");
  const auto trace_artifact_text = read_text(
      artifact_root / "memory-prod-logging-trace-4102444800000.json");
  const auto request_artifact_text = read_text(
      artifact_root / "memory-prod-logging-request-4102444800000.json");
  const auto artifact_text = session_artifact_text + trace_artifact_text +
                             request_artifact_text;
  const auto index_text = read_text(artifact_root / "query-index.jsonl");

  assert_true(session_query_result.ok && session_query_result.has_success_payload() &&
                  trace_query_result.ok && trace_query_result.has_success_payload() &&
                  request_query_result.ok && request_query_result.has_success_payload(),
              "memory production logging integration should materialize session, trace, and request query artifacts from persisted runtime.log");
  assert_true(session_query_result.match_count >= 2U,
              "memory production logging integration should query at least the writeback and context memory events by session_id");
  assert_true(trace_query_result.match_count >= 3U,
              "memory production logging integration should query writeback, context, and maintenance events by trace_id");
  assert_true(request_query_result.match_count >= 1U,
              "memory production logging integration should query the writeback event by request_id");
  assert_true(runtime_log_text.find("memory writeback.completed") != std::string::npos &&
                  runtime_log_text.find("memory context.assembled") != std::string::npos &&
                  runtime_log_text.find("memory maintenance.completed") != std::string::npos &&
                  runtime_log_text.find("memory init.completed") != std::string::npos &&
            runtime_log_text.find("memory shutdown.completed") != std::string::npos &&
            runtime_log_text.find("compression_strategy") != std::string::npos &&
            runtime_log_text.find("summarizer") != std::string::npos,
              "memory production logging integration should persist lifecycle, writeback, context, and maintenance events into runtime.log");
  assert_true(runtime_log_text.find(trace_id) != std::string::npos &&
                  runtime_log_text.find(writeback_request_id) != std::string::npos &&
                  runtime_log_text.find(context_request_id) != std::string::npos &&
                  runtime_log_text.find(maintenance_request_id) != std::string::npos,
              "memory production logging integration should persist safe trace and request correlation fields");
  assert_true(artifact_text.find("memory writeback.completed") != std::string::npos &&
                  artifact_text.find("memory context.assembled") != std::string::npos &&
                  artifact_text.find("memory maintenance.completed") != std::string::npos &&
            artifact_text.find(writeback_request_id) != std::string::npos &&
            artifact_text.find("compression_strategy") != std::string::npos &&
            artifact_text.find("summarizer") != std::string::npos,
              "memory production logging integration should keep correlated memory events queryable from artifact payloads");
  assert_true(index_text.find("memory-prod-logging-session") != std::string::npos &&
                  index_text.find("memory-prod-logging-trace") != std::string::npos &&
                  index_text.find("memory-prod-logging-request") != std::string::npos &&
                  index_text.find("session_id") != std::string::npos &&
                  index_text.find("trace_id") != std::string::npos &&
                  index_text.find("request_id") != std::string::npos,
              "memory production logging integration should append owner-safe selector metadata into the query index");
  assert_true(runtime_log_text.find("memory-secret-input") == std::string::npos &&
                  runtime_log_text.find("memory-secret-output") == std::string::npos &&
                  runtime_log_text.find("memory-secret-token") == std::string::npos &&
                  runtime_log_text.find("memory-secret-fact") == std::string::npos &&
                  runtime_log_text.find("memory-secret-goal") == std::string::npos &&
                  runtime_log_text.find("memory-secret-digest") == std::string::npos &&
                  runtime_log_text.find("retrieval-secret-ref") == std::string::npos,
              "memory production logging integration should not leak raw writeback/context/retrieval payloads into runtime.log");
  assert_true(artifact_text.find("memory-secret-input") == std::string::npos &&
                  artifact_text.find("memory-secret-output") == std::string::npos &&
                  artifact_text.find("memory-secret-token") == std::string::npos &&
                  artifact_text.find("memory-secret-fact") == std::string::npos &&
                  artifact_text.find("memory-secret-goal") == std::string::npos &&
                  artifact_text.find("memory-secret-digest") == std::string::npos &&
                  artifact_text.find("retrieval-secret-ref") == std::string::npos,
              "memory production logging integration should keep query artifacts redacted and free of raw owner payloads");

  cleanup_database_artifacts(database_path);
}

void test_memory_production_logging_asserts_partial_vector_maintenance_and_fallback_fields() {
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::audit::AuditService;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-memory-production-logging-fields");
  const auto database_path = temp_dir.path() / "memory-production-fields.db";
  const std::string session_id = "session-memory-production-fields";
  const std::string trace_id = "trace-memory-production-fields";
  cleanup_database_artifacts(database_path);

  ObservabilityLiveCompositionOptions options;
  options.profile_id = "desktop_full";
  options.metrics_granularity = "full";
  options.trace_sample_ratio = 1.0;
  options.logging_state_root_override = temp_dir.path();
  const auto observability = compose_live_observability(options);
  assert_true(observability.ok(),
        std::string("memory production logging field test should compose live providers: ") +
          observability.error);

  const auto logger =
    std::dynamic_pointer_cast<LoggingFacade>(observability.logger);
  const auto audit_service =
    std::dynamic_pointer_cast<AuditService>(observability.audit_logger);
  const auto metrics_facade =
    std::dynamic_pointer_cast<MetricsFacade>(observability.metrics_provider);
  auto recording_tracer = std::make_shared<RecordingTracer>();
  auto tracer_provider =
    std::make_shared<RecordingTracerProvider>(recording_tracer);
  assert_true(logger != nullptr && audit_service != nullptr &&
          metrics_facade != nullptr,
        "memory production logging field test should keep live log/audit/metrics sinks inspectable");

  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_handler(
      [](const dasall::llm::LLMGenerateRequest&) -> dasall::llm::LLMManagerResult {
    throw std::runtime_error("forced summarizer failure");
    });

  auto config = make_sqlite_config(database_path);
  config.vector.enabled = true;

  auto manager = dasall::memory::create_memory_manager(
    config,
    dasall::memory::MemoryRuntimeDependencies{
      .logger = observability.logger,
      .audit_logger = observability.audit_logger,
      .metrics_provider = observability.metrics_provider,
      .tracer_provider = tracer_provider,
      .summarizer_factory =
        [llm_manager](const dasall::memory::MemoryConfig&) {
        return std::make_unique<
          dasall::apps::runtime_support::LLMBackedSummarizer>(
          llm_manager);
        },
      .profile_id = "desktop_full",
    });
  assert_true(manager != nullptr,
        "memory production logging field test should create the manager with mixed live and recording sinks");
  assert_true(static_cast<int>(manager->init(config)) == 0,
        "memory production logging field test should initialize the sqlite-backed manager");

  auto partial_request = make_request(
    session_id,
    "turn-memory-production-fields-001",
    "partial writeback should remain observable",
    "agent response keeps the turn valid while summary is rejected",
    "partial writeback fact",
    77,
    false);
  partial_request.request_id = "req-memory-production-partial";
  partial_request.trace_id = trace_id;
  partial_request.summary_candidate->summary_text = "";
  const auto partial_result = manager->write_back(partial_request);
  assert_true(!partial_result.result_code.has_value() && partial_result.partial &&
          contains_value(partial_result.warnings, "summary_candidate_rejected") &&
          contains_value(partial_result.warnings, "partial_writeback_warning"),
        "partial writeback scenario should stay successful while surfacing rejected summary warnings");
  assert_true(logger->has_last_dispatched_event() &&
          logger->last_dispatched_event().attrs.at("event_name") ==
            "writeback.degraded" &&
          logger->last_dispatched_event().attrs.at("partial") == "true" &&
          contains_fragment(logger->last_dispatched_event().attrs.at("warning_codes"),
                  "summary_candidate_rejected"),
        "partial writeback scenario should keep partial and warning fields on the log event");
  assert_true(metrics_facade->last_recorded_sample().has_value() &&
          metrics_facade->last_recorded_sample()->identity_ref.name ==
            "memory_writeback_degraded_total" &&
          metrics_facade->last_recorded_sample()->labels.stage == "writeback" &&
          metrics_facade->last_recorded_sample()->labels.outcome == "degraded" &&
          metrics_facade->last_recorded_sample()->labels.error_code ==
            "summary_candidate_rejected",
        "partial writeback scenario should map degraded metric labels with the first warning as error_code");

  const auto partial_audit_export = audit_service->export_audit(make_audit_export_query());
  const auto* partial_audit_event =
    find_audit_event(partial_audit_export, "memory.writeback.degraded");
  assert_true(partial_audit_event != nullptr &&
          audit_event_has_side_effect(*partial_audit_event,
                        "field:partial=true") &&
          audit_event_has_side_effect(*partial_audit_event,
                        "field:warning_codes=summary_candidate_rejected,partial_writeback_warning"),
        "partial writeback scenario should project partial and warning fields into audit side_effects");
  const auto* partial_trace =
    find_started_span(recording_tracer->started_spans, "memory.writeback.degraded");
  assert_true(partial_trace != nullptr &&
          string_attr(partial_trace->descriptor.attrs, "partial") != nullptr &&
          *string_attr(partial_trace->descriptor.attrs, "partial") == "true" &&
          string_attr(partial_trace->descriptor.attrs, "warning_codes") != nullptr &&
          contains_fragment(*string_attr(partial_trace->descriptor.attrs, "warning_codes"),
                  "partial_writeback_warning") &&
          string_attr(partial_trace->descriptor.attrs, "trace_id") != nullptr &&
          *string_attr(partial_trace->descriptor.attrs, "trace_id") == trace_id,
        "partial writeback scenario should preserve partial and correlation fields on the trace span attrs");

  auto compression_seed_request = make_request(
    session_id,
    "turn-memory-production-fields-002",
    "second turn keeps compression fallback trigger deterministic",
    "agent response should keep the context assembly compression path active",
    "compression seed fact",
    79,
    false);
  compression_seed_request.request_id = "req-memory-production-compression-seed";
  compression_seed_request.trace_id = trace_id;
  const auto compression_seed_result = manager->write_back(compression_seed_request);
  assert_true(!compression_seed_result.result_code.has_value() &&
          !compression_seed_result.partial,
        "compression seed scenario should persist a second turn so context assembly enters the compression path");

  const auto context_result = manager->prepare_context(
    dasall::memory::MemoryContextRequest{
      .request_id = "req-memory-production-context-fallback",
      .session_id = session_id,
      .trace_id = trace_id,
      .stage = "reasoning",
      .goal_summary = "exercise vector-unavailable and summarizer-fallback observability",
      .constraints_summary = "keep emitted fields owner-safe and queryable",
      .latest_observation_digest_summary = "latest observation confirms the degraded path",
      .visible_tools = {"shell", "cmake"},
      .token_budget_hint = 240,
      .latency_budget_ms = 150,
      .external_evidence = {"runtime evidence for context degraded"},
      .retrieval_evidence_refs = {},
    });
  assert_true(!context_result.result_code.has_value() && context_result.degraded &&
          contains_value(context_result.warnings, "vector_unavailable") &&
          contains_value(context_result.compression_notes, "summarizer_fallback") &&
          contains_value(context_result.compression_notes, "strategy:template"),
        "context degraded scenario should keep vector unavailable and summarizer fallback evidence in the assembly result");
  assert_true(logger->has_last_dispatched_event() &&
          logger->last_dispatched_event().attrs.at("event_name") ==
            "context.degraded" &&
          logger->last_dispatched_event().attrs.at("compression_note_count") ==
            "2" &&
          logger->last_dispatched_event().attrs.at("compression_strategy") ==
            "template" &&
          contains_fragment(logger->last_dispatched_event().attrs.at("warning_codes"),
                  "vector_unavailable"),
        "context degraded scenario should project vector warning and template compression strategy into the log attrs");
  assert_true(metrics_facade->last_recorded_sample().has_value() &&
          metrics_facade->last_recorded_sample()->identity_ref.name ==
            "memory_context_degraded_total" &&
          metrics_facade->last_recorded_sample()->labels.stage ==
            "reasoning" &&
          metrics_facade->last_recorded_sample()->labels.outcome ==
            "degraded" &&
          metrics_facade->last_recorded_sample()->labels.error_code ==
            "vector_unavailable",
        "context degraded scenario should map vector-unavailable into degraded metric labels");
  const auto compression_snapshot = metrics_facade->aggregation_snapshot();
  const auto* compression_metric =
    compression_snapshot.find("memory_compression_applied_total");
  assert_true(compression_metric != nullptr && compression_metric->sample_count >= 1U,
        "context degraded scenario should still record the compression.applied metric when summarizer fallback occurs");

  const auto context_audit_export = audit_service->export_audit(make_audit_export_query());
  const auto* context_audit_event =
    find_audit_event(context_audit_export, "memory.context.degraded");
  assert_true(context_audit_event != nullptr &&
          audit_event_has_side_effect(*context_audit_event,
                        "field:compression_note_count=2") &&
          audit_event_has_side_effect(*context_audit_event,
                        "field:compression_strategy=template") &&
          audit_event_has_side_effect(*context_audit_event,
                        "field:warning_codes=vector_unavailable"),
        "context degraded scenario should keep template strategy and vector warning fields in audit side_effects");
  const auto* context_trace =
    find_started_span(recording_tracer->started_spans, "memory.context.degraded");
  assert_true(context_trace != nullptr &&
          string_attr(context_trace->descriptor.attrs, "compression_note_count") != nullptr &&
          *string_attr(context_trace->descriptor.attrs, "compression_note_count") ==
            "2" &&
          string_attr(context_trace->descriptor.attrs, "compression_strategy") != nullptr &&
          *string_attr(context_trace->descriptor.attrs, "compression_strategy") ==
            "template" &&
          string_attr(context_trace->descriptor.attrs, "warning_codes") != nullptr &&
          contains_fragment(*string_attr(context_trace->descriptor.attrs, "warning_codes"),
                  "vector_unavailable") &&
          string_attr(context_trace->descriptor.attrs, "event_name") != nullptr &&
          *string_attr(context_trace->descriptor.attrs, "event_name") ==
            "memory.context.degraded",
        "context degraded scenario should preserve template strategy and vector warning fields on the trace span attrs");

  const auto maintenance_report = manager->run_maintenance(
    dasall::memory::MaintenanceRequest{
      .run_checkpoint = true,
      .run_retention = true,
      .run_quarantine_cleanup = true,
      .run_vector_rebuild = true,
      .request_id = "req-memory-production-maintenance-tick",
      .trace_id = trace_id,
    });
  assert_true(contains_value(maintenance_report.warnings, "vector_rebuild_skipped"),
        "maintenance degraded scenario should surface the unavailable vector rebuild warning");
  assert_true(logger->has_last_dispatched_event() &&
          logger->last_dispatched_event().attrs.at("event_name") ==
            "maintenance.degraded" &&
          logger->last_dispatched_event().attrs.at("checkpoint_requested") ==
            "true" &&
          logger->last_dispatched_event().attrs.at("retention_requested") ==
            "true" &&
          logger->last_dispatched_event().attrs.at("quarantine_requested") ==
            "true" &&
          logger->last_dispatched_event().attrs.at("vector_rebuild_requested") ==
            "true" &&
          contains_fragment(logger->last_dispatched_event().attrs.at("warning_codes"),
                  "vector_rebuild_skipped"),
        "maintenance degraded scenario should retain maintenance-tick request fields on the log attrs");
  assert_true(metrics_facade->last_recorded_sample().has_value() &&
          metrics_facade->last_recorded_sample()->identity_ref.name ==
            "memory_maintenance_degraded_total" &&
          metrics_facade->last_recorded_sample()->labels.stage ==
            "maintenance" &&
          metrics_facade->last_recorded_sample()->labels.outcome ==
            "degraded" &&
          metrics_facade->last_recorded_sample()->labels.error_code ==
            "vector_rebuild_skipped",
        "maintenance degraded scenario should map maintenance warning fields into degraded metric labels");

  const auto maintenance_audit_export = audit_service->export_audit(make_audit_export_query());
  const auto* maintenance_audit_event =
    find_audit_event(maintenance_audit_export, "memory.maintenance.degraded");
  assert_true(maintenance_audit_event != nullptr &&
          audit_event_has_side_effect(*maintenance_audit_event,
                        "field:checkpoint_requested=true") &&
          audit_event_has_side_effect(*maintenance_audit_event,
                        "field:retention_requested=true") &&
          audit_event_has_side_effect(*maintenance_audit_event,
                        "field:quarantine_requested=true") &&
          audit_event_has_side_effect(*maintenance_audit_event,
                        "field:vector_rebuild_requested=true") &&
          audit_event_has_side_effect(*maintenance_audit_event,
                        "field:warning_codes=vector_rebuild_skipped"),
        "maintenance degraded scenario should keep maintenance request fields inside audit side_effects");
  const auto* maintenance_trace =
    find_started_span(recording_tracer->started_spans, "memory.maintenance.degraded");
  assert_true(maintenance_trace != nullptr &&
          string_attr(maintenance_trace->descriptor.attrs, "checkpoint_requested") != nullptr &&
          *string_attr(maintenance_trace->descriptor.attrs, "checkpoint_requested") ==
            "true" &&
          string_attr(maintenance_trace->descriptor.attrs, "retention_requested") != nullptr &&
          *string_attr(maintenance_trace->descriptor.attrs, "retention_requested") ==
            "true" &&
          string_attr(maintenance_trace->descriptor.attrs, "quarantine_requested") != nullptr &&
          *string_attr(maintenance_trace->descriptor.attrs, "quarantine_requested") ==
            "true" &&
          string_attr(maintenance_trace->descriptor.attrs, "vector_rebuild_requested") != nullptr &&
          *string_attr(maintenance_trace->descriptor.attrs, "vector_rebuild_requested") ==
            "true" &&
          string_attr(maintenance_trace->descriptor.attrs, "warning_codes") != nullptr &&
          contains_fragment(*string_attr(maintenance_trace->descriptor.attrs, "warning_codes"),
                  "vector_rebuild_skipped"),
        "maintenance degraded scenario should project maintenance request and warning fields into trace attrs");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_memory_production_logging_asserts_schema_mismatch_fields() {
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::audit::AuditService;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-memory-production-schema-mismatch");
  const auto database_path = temp_dir.path() / "memory-production-schema-mismatch.db";
  cleanup_database_artifacts(database_path);

  ObservabilityLiveCompositionOptions options;
  options.profile_id = "desktop_full";
  options.metrics_granularity = "full";
  options.trace_sample_ratio = 1.0;
  options.logging_state_root_override = temp_dir.path();
  const auto observability = compose_live_observability(options);
  assert_true(observability.ok(),
        std::string("memory production schema mismatch test should compose live providers: ") +
          observability.error);

  const auto logger =
    std::dynamic_pointer_cast<LoggingFacade>(observability.logger);
  const auto audit_service =
    std::dynamic_pointer_cast<AuditService>(observability.audit_logger);
  const auto metrics_facade =
    std::dynamic_pointer_cast<MetricsFacade>(observability.metrics_provider);
  auto recording_tracer = std::make_shared<RecordingTracer>();
  auto tracer_provider =
    std::make_shared<RecordingTracerProvider>(recording_tracer);
  assert_true(logger != nullptr && audit_service != nullptr &&
          metrics_facade != nullptr,
        "memory production schema mismatch test should keep live log/audit/metrics sinks inspectable");

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(
    config,
    dasall::memory::MemoryRuntimeDependencies{
      .logger = observability.logger,
      .audit_logger = observability.audit_logger,
      .metrics_provider = observability.metrics_provider,
      .tracer_provider = tracer_provider,
      .profile_id = "desktop_full",
    });
  assert_true(static_cast<int>(manager->init(config)) == 0,
        "schema mismatch setup should initialize the sqlite-backed manager once");
  manager->shutdown();

  execute_sql(database_path,
        "UPDATE schema_migrations SET checksum = 'tampered' WHERE version = 1");

  auto mismatched_manager = dasall::memory::create_memory_manager(
    config,
    dasall::memory::MemoryRuntimeDependencies{
      .logger = observability.logger,
      .audit_logger = observability.audit_logger,
      .metrics_provider = observability.metrics_provider,
      .tracer_provider = tracer_provider,
      .profile_id = "desktop_full",
    });
  const auto init_code = mismatched_manager->init(config);
  const auto expected_result_code =
    dasall::memory::map_memory_error(dasall::memory::MemoryError::SchemaMismatch)
      .result_code;
  const auto expected_result_code_string =
    std::to_string(static_cast<int>(expected_result_code));
  assert_true(init_code == expected_result_code,
        "schema mismatch scenario should surface the schema mismatch result code through init");
  assert_true(logger->has_last_dispatched_event(),
        "schema mismatch scenario should emit an init.failed log event");
  const auto& init_failed_log_event = logger->last_dispatched_event();
  dasall::tests::support::assert_equal(
    std::string("init.failed"),
    init_failed_log_event.attrs.at("event_name"),
    "schema mismatch scenario should keep the init.failed event name on the log event");
  dasall::tests::support::assert_equal(
    std::string("failed"),
    init_failed_log_event.attrs.at("lifecycle_state"),
    "schema mismatch scenario should keep lifecycle_state=failed on the init.failed log event");
  dasall::tests::support::assert_equal(
    expected_result_code_string,
    init_failed_log_event.attrs.at("result_code"),
    "schema mismatch scenario should keep the schema mismatch result_code on the init.failed log event");
  dasall::tests::support::assert_equal(
    std::string("store_preopen_failed"),
    init_failed_log_event.attrs.at("failure_reason"),
    "schema mismatch scenario should keep failure_reason=store_preopen_failed on the init.failed log event");
  assert_true(metrics_facade->last_recorded_sample().has_value() &&
          metrics_facade->last_recorded_sample()->identity_ref.name ==
            "memory_init_failed_total" &&
          metrics_facade->last_recorded_sample()->labels.stage == "init" &&
          metrics_facade->last_recorded_sample()->labels.outcome == "failure" &&
          metrics_facade->last_recorded_sample()->labels.error_code ==
            expected_result_code_string,
        "schema mismatch scenario should map init failure fields into metric labels");

  const auto audit_export = audit_service->export_audit(make_audit_export_query());
  const auto* audit_event = find_audit_event(audit_export, "memory.init.failed");
  assert_true(audit_event != nullptr &&
          audit_event_has_side_effect(*audit_event,
                        "field:lifecycle_state=failed") &&
          audit_event_has_side_effect(*audit_event,
                        "field:result_code=" + expected_result_code_string) &&
          audit_event_has_side_effect(*audit_event,
        "field:failure_reason=store_preopen_failed") &&
          audit_event_has_side_effect(*audit_event,
                        "field:storage_backend=sqlite"),
        "schema mismatch scenario should project lifecycle and storage failure fields into audit side_effects");
  const auto* trace_event =
    find_started_span(recording_tracer->started_spans, "memory.init.failed");
  assert_true(trace_event != nullptr &&
          string_attr(trace_event->descriptor.attrs, "lifecycle_state") != nullptr &&
          *string_attr(trace_event->descriptor.attrs, "lifecycle_state") ==
            "failed" &&
          string_attr(trace_event->descriptor.attrs, "result_code") != nullptr &&
          *string_attr(trace_event->descriptor.attrs, "result_code") ==
            expected_result_code_string &&
          string_attr(trace_event->descriptor.attrs, "failure_reason") != nullptr &&
          *string_attr(trace_event->descriptor.attrs, "failure_reason") ==
            "store_preopen_failed" &&
          string_attr(trace_event->descriptor.attrs, "storage_backend") != nullptr &&
          *string_attr(trace_event->descriptor.attrs, "storage_backend") ==
            "sqlite",
        "schema mismatch scenario should preserve lifecycle and storage failure fields on the init.failed trace attrs");

  mismatched_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_production_logging_persists_queryable_redacted_events();
  test_memory_production_logging_asserts_partial_vector_maintenance_and_fallback_fields();
  test_memory_production_logging_asserts_schema_mismatch_fields();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}