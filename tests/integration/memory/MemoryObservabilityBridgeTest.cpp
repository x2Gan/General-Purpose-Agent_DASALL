#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "ObservabilityLiveComposition.h"
#include "audit/AuditService.h"
#include "logging/LoggingFacade.h"
#include "metrics/MetricsFacade.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"
#include "tracing/TracerProviderImpl.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-observability-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

void execute_sql(const std::filesystem::path& database_path, const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for observability test");
  }

  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message =
        error_message == nullptr ? "failed to execute sqlite statement"
                                 : error_message;
    sqlite3_free(error_message);
    sqlite3_close(connection);
    throw std::runtime_error(message);
  }

  sqlite3_close(connection);
}

void seed_old_quarantine_record(const std::filesystem::path& database_path,
                                const std::string& object_id) {
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.maintenance.quarantine_ttl_ms = 1000;

  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to open sqlite store for observability quarantine seed");
  }

  if (!store->quarantine_record("turn", object_id, "observability-seed").ok) {
    throw std::runtime_error("failed to seed quarantine record for observability test");
  }
  store->close();

  execute_sql(database_path,
              "UPDATE quarantined_records SET created_at = 1 WHERE object_id = '" +
                  object_id + "'");
}

[[nodiscard]] bool export_has_action(const dasall::infra::ExportResult& result,
                                     const std::string& expected_action) {
  return std::any_of(result.records.begin(), result.records.end(),
                     [&expected_action](const auto& record) {
                       return record.action == expected_action;
                     });
}

[[nodiscard]] bool has_attr(const dasall::infra::LogEvent::AttributeMap& attrs,
                            const std::string& key) {
  return attrs.find(key) != attrs.end();
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
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
  request.summary_candidate->summary_text = std::string{"summary for "} + turn_id;
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
        "supersede relation should remain auditable";
    experience_candidate.experience.trigger_condition = "fact conflict detected";
    experience_candidate.experience.recommended_action =
        "record supersede relation and continue";
    experience_candidate.experience.created_at = 5000;
    experience_candidate.extraction_source = "reflection";
    request.experience_candidates.push_back(std::move(experience_candidate));
  }

  return request;
}

void test_memory_observability_bridge_emits_context_writeback_conflict_and_maintenance_events() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  const auto observability = dasall::infra::compose_live_observability(
      dasall::infra::ObservabilityLiveCompositionOptions{
          .profile_id = "desktop_full",
          .metrics_granularity = "full",
          .trace_sample_ratio = 1.0,
      });
  assert_true(observability.ok(),
              std::string("memory observability test should compose live providers: ") +
                  observability.error);

  const auto logger =
      std::dynamic_pointer_cast<dasall::infra::logging::LoggingFacade>(observability.logger);
  const auto audit_service =
      std::dynamic_pointer_cast<dasall::infra::audit::AuditService>(observability.audit_logger);
  const auto metrics_facade =
      std::dynamic_pointer_cast<dasall::infra::metrics::MetricsFacade>(observability.metrics_provider);
  const auto tracer_provider = std::dynamic_pointer_cast<
      dasall::infra::tracing::TracerProviderImpl>(observability.tracer_provider);
  assert_true(logger != nullptr && audit_service != nullptr &&
                  metrics_facade != nullptr && tracer_provider != nullptr,
              "memory observability test should keep concrete providers inspectable");

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
              "memory observability test should create a manager with live sinks");

  const auto pre_init_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
          .request_id = "req-memory-obs-preinit",
          .session_id = "session-memory-obs",
          .trace_id = "trace-memory-obs",
          .stage = "planning",
          .goal_summary = "pre-init failure should be observable",
      });
  assert_true(pre_init_result.result_code.has_value(),
              "prepare_context before init should emit a front-door failure");

  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory observability test should initialize the sqlite-backed manager");

  const auto first_writeback = manager->write_back(make_request(
      "session-memory-obs",
      "turn-memory-obs-001",
      "remember current network state",
      "first writeback persisted network enabled",
      "network mode enabled",
      70,
      false));
  assert_true(!first_writeback.result_code.has_value(),
              "first writeback should succeed on the observability path");

  const auto second_writeback = manager->write_back(make_request(
      "session-memory-obs",
      "turn-memory-obs-002",
      "update current network state",
      "second writeback persisted network disabled",
      "network mode disabled",
      95,
      true));
  assert_true(!second_writeback.result_code.has_value() &&
                  second_writeback.conflicts.size() == 1U,
              "second writeback should surface a supersede conflict on the observability path");

  const auto context_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
          .request_id = "req-memory-obs-context",
          .session_id = "session-memory-obs",
          .trace_id = "trace-memory-obs",
          .stage = "reasoning",
          .goal_summary = "prove memory production observability sinks",
          .constraints_summary = "keep goal and latest observation visible",
          .latest_observation_digest_summary = "latest observation confirms sqlite writeback path is live",
          .visible_tools = {"shell", "cmake"},
          .token_budget_hint = 220,
          .latency_budget_ms = 100,
          .external_evidence = {"runtime evidence: memory observability"},
          .retrieval_evidence_refs = {},
      });
  assert_true(!context_result.result_code.has_value() &&
                  !context_result.compression_notes.empty(),
              "context assembly should succeed and emit compression notes on the observability path");

  seed_old_quarantine_record(database_path, "memory-observability-quarantine");
  const auto maintenance_report = manager->run_maintenance(
      dasall::memory::MaintenanceRequest{
          .run_checkpoint = false,
          .run_retention = false,
          .run_quarantine_cleanup = true,
          .run_vector_rebuild = false,
          .request_id = "req-memory-obs-maintenance",
          .trace_id = "trace-memory-obs",
      });
  assert_true(maintenance_report.quarantine_cleaned == 1,
              "maintenance should clean the seeded quarantine row on the observability path");

  assert_true(logger->has_last_dispatched_event() &&
                  logger->last_dispatched_event().attrs.at("event_name") ==
                      "maintenance.completed" &&
                  logger->last_dispatched_event().attrs.at("request_id") ==
                      "req-memory-obs-maintenance" &&
                  logger->last_dispatched_event().attrs.at("trace_id") ==
                      "trace-memory-obs",
              "maintenance telemetry should preserve request and trace correlation on the final event");

  auto invalid_writeback_request = make_request(
      "session-memory-obs",
      "turn-memory-obs-invalid",
      "invalid turn should still be observable",
      "transient response",
      "invalid fact should not persist",
      50,
      false);
  invalid_writeback_request.request_id = "req-memory-obs-invalid-writeback";
  invalid_writeback_request.trace_id = "trace-memory-obs";
  invalid_writeback_request.turn.agent_response = "";
  const auto invalid_writeback = manager->write_back(invalid_writeback_request);
  assert_true(invalid_writeback.result_code.has_value(),
              "invalid writeback should fail validation before storage persistence");
  assert_true(logger->has_last_dispatched_event() &&
                  logger->last_dispatched_event().attrs.at("event_name") ==
                      "writeback.failed" &&
                  logger->last_dispatched_event().attrs.at("request_id") ==
                      "req-memory-obs-invalid-writeback" &&
                  logger->last_dispatched_event().attrs.at("trace_id") ==
                      "trace-memory-obs" &&
                  logger->last_dispatched_event().attrs.at("warning_codes") ==
                      "writeback_turn_invalid" &&
                  logger->last_dispatched_event().attrs.at("failure_reason") ==
                      "agent_response must be non-empty when present",
              "invalid writeback telemetry should expose safe warning codes and validation reason");

  const auto audit_export = audit_service->export_audit(dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  });

  assert_true(export_has_action(audit_export, "memory.context.failed"),
              "audit export should include the front-door context failure event");
  assert_true(export_has_action(audit_export, "memory.init.completed"),
              "audit export should include the memory lifecycle initialization event");
  assert_true(export_has_action(audit_export, "memory.writeback.completed"),
              "audit export should include a successful writeback event");
  assert_true(export_has_action(audit_export, "memory.writeback.failed"),
              "audit export should include the invalid writeback validation failure event");
  assert_true(export_has_action(audit_export, "memory.writeback.degraded"),
              "audit export should include a degraded writeback event");
  assert_true(export_has_action(audit_export, "memory.conflict.superseded"),
              "audit export should include a fact supersede conflict event");
  assert_true(export_has_action(audit_export, "memory.compression.applied"),
              "audit export should include a compression event");
  assert_true(export_has_action(audit_export, "memory.context.assembled"),
              "audit export should include a successful context assembly event");
  assert_true(export_has_action(audit_export, "memory.maintenance.completed"),
              "audit export should include a successful maintenance event");

  assert_true(logger->dispatched_record_count() >= 9U,
              "logging facade should receive multiple memory telemetry events");
  assert_true(logger->has_last_dispatched_event(),
              "logging facade should retain the last dispatched memory telemetry event");
  assert_true(logger->last_dispatched_event().attrs.at("event_name") ==
                  "writeback.failed",
              "invalid writeback should be the last emitted memory telemetry event in this scenario");
  assert_true(!has_attr(logger->last_dispatched_event().attrs, "summary_text") &&
                  !has_attr(logger->last_dispatched_event().attrs, "goal_summary") &&
                  !has_attr(logger->last_dispatched_event().attrs,
                            "latest_observation_digest_summary") &&
                  !has_attr(logger->last_dispatched_event().attrs, "agent_response") &&
                  !has_attr(logger->last_dispatched_event().attrs, "fact_text"),
              "memory observability bridge should not leak raw context or writeback payload fields into log attrs");
  assert_true(metrics_facade->record_attempt_count() > 0U,
              "metrics facade should record memory telemetry samples");
  assert_true(tracer_provider->tracer_count() > 0U,
              "tracer provider should open at least one memory tracer scope");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_observability_bridge_emits_context_writeback_conflict_and_maintenance_events();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}