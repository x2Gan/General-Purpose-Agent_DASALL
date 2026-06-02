#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

constexpr int kSoakIterationCount = 320;
constexpr int kSoakBatchSize = 8;
constexpr int kSummaryInterval = 64;
constexpr std::uintmax_t kMaxWalBytes = 2U * 1024U * 1024U;

std::filesystem::path make_temp_database_path(const std::string& stem) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         (stem + "-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void execute_sql(const std::filesystem::path& database_path,
                 const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for long-running soak test");
  }

  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message = error_message == nullptr
                                    ? "failed to execute sqlite statement"
                                    : error_message;
    sqlite3_free(error_message);
    sqlite3_close(connection);
    throw std::runtime_error(message);
  }

  sqlite3_close(connection);
}

int query_scalar_count(const std::filesystem::path& database_path,
                       const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for count query");
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    sqlite3_close(connection);
    throw std::runtime_error("failed to prepare sqlite count query");
  }

  int value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }

  sqlite3_finalize(statement);
  sqlite3_close(connection);
  return value;
}

[[nodiscard]] bool has_warning(const std::vector<std::string>& warnings,
                               const std::string& warning_key) {
  return std::find(warnings.begin(), warnings.end(), warning_key) != warnings.end();
}

[[nodiscard]] std::uintmax_t wal_size_bytes(
    const std::filesystem::path& database_path) {
  const auto wal_path = std::filesystem::path(database_path.string() + "-wal");
  if (!std::filesystem::exists(wal_path)) {
    return 0;
  }
  return std::filesystem::file_size(wal_path);
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.storage.wal_autocheckpoint_pages = 8;
  config.storage.busy_timeout_ms = 100;
  config.context.compression_trigger_turns = 6;
  config.context.compression_trigger_ratio = 0.5;
  config.maintenance.retention_turns = 24;
  config.maintenance.fact_ttl_ms = 5000;
  config.maintenance.experience_ttl_ms = 5000;
  config.maintenance.quarantine_ttl_ms = 5000;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_soak_request(
    int iteration) {
  const auto simulated_time = current_time_millis() - 300000 + (iteration * 1000);
  const std::string session_id = "session-memory-long-running-soak";
  const std::string turn_id =
      "turn-memory-long-running-soak-" + std::to_string(iteration);

  dasall::memory::MemoryWritebackRequest request;
  request.request_id =
      "writeback-memory-long-running-soak-" + std::to_string(iteration);
  request.session_id = session_id;
  request.trace_id = "trace-memory-long-running-soak";
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = "soak iteration " + std::to_string(iteration);
  request.turn.agent_response =
      (iteration % 2) == 0 ? "soak mode enabled" : "soak mode disabled";
  request.turn.created_at = simulated_time;

  if ((iteration % kSummaryInterval) == 0) {
    request.summary_candidate = dasall::contracts::SummaryMemory{};
    request.summary_candidate->summary_text = "summary for " + turn_id;
    request.summary_candidate->source_turn_ids = std::vector<std::string>{turn_id};
    request.summary_candidate->confirmed_facts =
        std::vector<std::string>{"soak cadence checkpoint"};
    request.summary_candidate->created_at = simulated_time;
  }

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text =
      (iteration % 2) == 0 ? "soak mode enabled" : "soak mode disabled";
  fact_candidate.fact.fact_type = "soak_state";
  fact_candidate.fact.confidence_score = 90;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.fact.created_at = simulated_time;
  fact_candidate.extraction_source = "soak";
  request.fact_candidates.push_back(std::move(fact_candidate));

  return request;
}

void seed_stale_retention_records(
    dasall::memory::store::sqlite::SqliteMemoryStore& store,
    const std::filesystem::path& database_path,
    int batch_index,
    const std::string& session_id,
    const std::string& source_turn_id) {
  using dasall::tests::support::assert_true;

  dasall::contracts::MemoryFact stale_fact;
  stale_fact.fact_id = "stale-fact-soak-" + std::to_string(batch_index);
  stale_fact.session_id = session_id;
  stale_fact.fact_text = "stale soak fact";
  stale_fact.source_turn_ids = std::vector<std::string>{source_turn_id};
  stale_fact.confidence_score = 60;
  stale_fact.fact_type = "soak_state";
  stale_fact.created_at = 1;
  assert_true(store.insert_fact(stale_fact).ok,
              "long-running soak should seed a stale superseded fact for retention coverage");
  assert_true(
      store.supersede_fact(*stale_fact.fact_id,
                           "fresh-fact-soak-" + std::to_string(batch_index))
          .ok,
      "long-running soak should mark the seeded stale fact as superseded");

  dasall::contracts::ExperienceMemory stale_experience;
  stale_experience.experience_id =
      "stale-experience-soak-" + std::to_string(batch_index);
  stale_experience.session_id = session_id;
  stale_experience.lesson_summary = "drop the stale soak experience";
  stale_experience.trigger_condition = "soak retention cleanup";
  stale_experience.recommended_action = "purge superseded expired experience";
  stale_experience.source_turn_ids = std::vector<std::string>{source_turn_id};
  stale_experience.created_at = 1;
  stale_experience.expires_at = 2;
  stale_experience.superseded_by_experience_id =
      "fresh-experience-soak-" + std::to_string(batch_index);
  assert_true(store.insert_experience(stale_experience).ok,
              "long-running soak should seed a stale superseded experience for retention coverage");

  const std::string quarantine_id =
      "stale-quarantine-soak-" + std::to_string(batch_index);
  assert_true(store.quarantine_record("turn", quarantine_id, "soak-retention").ok,
              "long-running soak should seed a stale quarantine record");
  execute_sql(database_path,
              "UPDATE quarantined_records SET created_at = 1 WHERE object_id = '" +
                  quarantine_id + "'");
}

void test_memory_manager_long_running_soak_keeps_wal_and_retention_bounded() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-long-running-soak");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error(
        "DASALL_SQL_MEMORY_DIR must be defined for memory long-running soak coverage");
  }

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_equal(0, static_cast<int>(init_code),
               "long-running soak requires a sqlite-backed memory manager");

  dasall::memory::store::sqlite::SqliteMemoryStore seeding_store;
  assert_true(!seeding_store.open(config).has_value(),
              "long-running soak requires a direct sqlite store for stale retention seeding");

  std::uintmax_t max_wal_bytes = 0;
  int total_turns_purged = 0;
  int total_facts_purged = 0;
  int total_experiences_purged = 0;
  int total_quarantine_cleaned = 0;
  bool checkpoint_executed = false;
  std::string latest_turn_id;

  for (int batch_start = 0; batch_start < kSoakIterationCount;
       batch_start += kSoakBatchSize) {
    const int batch_end = std::min(batch_start + kSoakBatchSize, kSoakIterationCount);
    for (int iteration = batch_start; iteration < batch_end; ++iteration) {
      const auto result = manager->write_back(make_soak_request(iteration));
      assert_true(!result.result_code.has_value() && result.persisted_turn_id.has_value(),
                  "long-running soak should keep write_back healthy across the compressed window");
      latest_turn_id = *result.persisted_turn_id;
    }

    seed_stale_retention_records(seeding_store, database_path,
                                 batch_start / kSoakBatchSize,
                                 "session-memory-long-running-soak",
                                 latest_turn_id);

    dasall::memory::MaintenanceRequest request;
    request.request_id =
        "maintenance-memory-long-running-soak-" + std::to_string(batch_start);
    request.trace_id = "trace-memory-long-running-soak";
    request.run_checkpoint = true;
    request.run_retention = true;
    request.run_quarantine_cleanup = true;
    request.run_vector_rebuild = false;

    const auto report = manager->run_maintenance(request);
    assert_true(!has_warning(report.warnings, "maintenance_store_not_open"),
                "long-running soak should keep the maintenance store wired");
    assert_true(!has_warning(report.warnings, "checkpoint_starvation"),
                "long-running soak should avoid WAL checkpoint starvation");
    assert_true(!has_warning(report.warnings, "turn_retention_failed") &&
                    !has_warning(report.warnings, "turn_retention_commit_failed") &&
                    !has_warning(report.warnings, "fact_retention_failed") &&
                    !has_warning(report.warnings, "experience_retention_failed") &&
                    !has_warning(report.warnings, "quarantine_cleanup_failed"),
                "long-running soak should complete retention and quarantine cleanup without storage warnings");

    checkpoint_executed = checkpoint_executed || report.checkpoint_executed;
    total_turns_purged += report.turns_purged;
    total_facts_purged += report.facts_purged;
    total_experiences_purged += report.experiences_purged;
    total_quarantine_cleaned += report.quarantine_cleaned;
    max_wal_bytes = std::max(max_wal_bytes, wal_size_bytes(database_path));
  }

  assert_true(checkpoint_executed,
              "long-running soak should execute at least one WAL checkpoint");
  assert_true(total_turns_purged > 0,
              "long-running soak should purge at least one old turn through retention");
  assert_true(total_facts_purged >= (kSoakIterationCount / kSoakBatchSize),
              "long-running soak should purge each seeded stale fact");
  assert_true(total_experiences_purged >= (kSoakIterationCount / kSoakBatchSize),
              "long-running soak should purge each seeded stale experience");
  assert_true(total_quarantine_cleaned >= (kSoakIterationCount / kSoakBatchSize),
              "long-running soak should clean each seeded stale quarantine record");
  assert_true(max_wal_bytes <= kMaxWalBytes,
              "long-running soak should keep WAL growth bounded under the configured checkpoint cadence");

  const auto bundle = seeding_store.load_session_bundle(
      dasall::memory::SessionLoadRequest{
          .session_id = "session-memory-long-running-soak",
          .recent_turn_limit = 128,
      });
  const int max_expected_turns =
      config.maintenance.retention_turns +
      (kSoakIterationCount / kSummaryInterval) + kSoakBatchSize;
  assert_true(bundle.total_turn_count <= max_expected_turns,
              "long-running soak should keep retained turns bounded after repeated maintenance");

  assert_equal(0,
               query_scalar_count(
                   database_path,
                   "SELECT COUNT(*) FROM facts WHERE fact_id LIKE 'stale-fact-soak-%'"),
               "long-running soak should remove all seeded stale fact rows");
  assert_equal(
      0,
      query_scalar_count(database_path,
                         "SELECT COUNT(*) FROM experiences WHERE experience_id LIKE 'stale-experience-soak-%'"),
      "long-running soak should remove all seeded stale experience rows");
  assert_equal(
      0,
      query_scalar_count(database_path,
                         "SELECT COUNT(*) FROM quarantined_records WHERE object_id LIKE 'stale-quarantine-soak-%'"),
      "long-running soak should remove all seeded stale quarantine rows");

  seeding_store.close();
  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_manager_long_running_soak_keeps_wal_and_retention_bounded();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}