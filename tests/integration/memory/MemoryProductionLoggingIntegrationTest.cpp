#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "ObservabilityLiveComposition.h"
#include "logging/FileLogReader.h"
#include "logging/LogQueryService.h"
#include "logging/LoggingFacade.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

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
  auto manager = dasall::memory::create_memory_manager(
      config,
      dasall::memory::MemoryRuntimeDependencies{
          .logger = observability.logger,
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
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
                  runtime_log_text.find("memory shutdown.completed") != std::string::npos,
              "memory production logging integration should persist lifecycle, writeback, context, and maintenance events into runtime.log");
  assert_true(runtime_log_text.find(trace_id) != std::string::npos &&
                  runtime_log_text.find(writeback_request_id) != std::string::npos &&
                  runtime_log_text.find(context_request_id) != std::string::npos &&
                  runtime_log_text.find(maintenance_request_id) != std::string::npos,
              "memory production logging integration should persist safe trace and request correlation fields");
  assert_true(artifact_text.find("memory writeback.completed") != std::string::npos &&
                  artifact_text.find("memory context.assembled") != std::string::npos &&
                  artifact_text.find("memory maintenance.completed") != std::string::npos &&
                  artifact_text.find(writeback_request_id) != std::string::npos,
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

}  // namespace

int main() {
  try {
    test_memory_production_logging_persists_queryable_redacted_events();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}