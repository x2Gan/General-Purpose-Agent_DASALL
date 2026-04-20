#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "maintenance/MemoryMaintenanceWorker.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-maintenance-retention-" +
          std::to_string(timestamp) + ".db");
}

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

void execute_sql(const std::filesystem::path& database_path, const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for maintenance test");
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
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
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

dasall::memory::MemoryConfig make_sqlite_config(const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.maintenance.retention_turns = 2;
  config.maintenance.fact_ttl_ms = 1000;
  config.maintenance.experience_ttl_ms = 1000;
  config.maintenance.quarantine_ttl_ms = 1000;
  return config;
}

void test_memory_maintenance_worker_applies_retention_and_cleanup() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_sqlite_config(database_path);
  const auto now_millis = current_time_millis();

  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error(
        "DASALL_SQL_MEMORY_DIR must be defined for maintenance retention coverage");
  }

  assert_true(!store->open(config).has_value(),
              "sqlite store should open for maintenance retention coverage");

  dasall::contracts::Session session;
  session.session_id = "session-024-retention";
  session.turn_ids = std::vector<std::string>{};
  session.user_id = "user-024";
  session.created_at = now_millis - 5000;
  session.last_active_at = now_millis - 4000;
  assert_true(store->create_session(session).ok,
              "maintenance retention test should create a session");

  dasall::contracts::Turn first_turn;
  first_turn.turn_id = "turn-retention-001";
  first_turn.session_id = "session-024-retention";
  first_turn.user_input = "first turn";
  first_turn.created_at = now_millis - 4000;

  dasall::contracts::Turn second_turn;
  second_turn.turn_id = "turn-retention-002";
  second_turn.session_id = "session-024-retention";
  second_turn.user_input = "second turn";
  second_turn.created_at = now_millis - 3000;

  dasall::contracts::Turn third_turn;
  third_turn.turn_id = "turn-retention-003";
  third_turn.session_id = "session-024-retention";
  third_turn.user_input = "third turn";
  third_turn.created_at = now_millis - 2000;

  assert_true(store->append_turn(first_turn).ok,
              "maintenance retention test should persist the first turn");
  assert_true(store->append_turn(second_turn).ok,
              "maintenance retention test should persist the second turn");
  assert_true(store->append_turn(third_turn).ok,
              "maintenance retention test should persist the third turn");

  dasall::contracts::SummaryMemory protected_summary;
  protected_summary.summary_id = "summary-retention-001";
  protected_summary.session_id = "session-024-retention";
  protected_summary.summary_text = "protect the oldest turn";
  protected_summary.source_turn_ids =
      std::vector<std::string>{"turn-retention-001"};
  protected_summary.created_at = now_millis - 1500;
  assert_true(store->upsert_summary(protected_summary).ok,
              "maintenance retention test should persist a protecting summary");

  dasall::contracts::MemoryFact retained_fact;
  retained_fact.fact_id = "fact-retention-keep";
  retained_fact.session_id = "session-024-retention";
  retained_fact.fact_text = "keep the latest durable fact";
  retained_fact.source_turn_ids = std::vector<std::string>{"turn-retention-003"};
  retained_fact.confidence_score = 95;
  retained_fact.created_at = now_millis - 1500;
  retained_fact.fact_type = "state";
  retained_fact.valid_until = now_millis + 60000;
  assert_true(store->insert_fact(retained_fact).ok,
              "maintenance retention test should persist the retained fact");

  dasall::contracts::MemoryFact stale_fact;
  stale_fact.fact_id = "fact-retention-stale";
  stale_fact.session_id = "session-024-retention";
  stale_fact.fact_text = "drop the superseded stale fact";
  stale_fact.source_turn_ids = std::vector<std::string>{"turn-retention-002"};
  stale_fact.confidence_score = 70;
  stale_fact.created_at = now_millis - 4000;
  stale_fact.fact_type = "state";
  stale_fact.valid_until = now_millis - 500;
  assert_true(store->insert_fact(stale_fact).ok,
              "maintenance retention test should persist the stale fact");
  assert_true(store->supersede_fact("fact-retention-stale", "fact-retention-next").ok,
              "maintenance retention test should mark the stale fact as superseded");

  dasall::contracts::ExperienceMemory retained_experience;
  retained_experience.experience_id = "experience-retention-keep";
  retained_experience.session_id = "session-024-retention";
  retained_experience.lesson_summary = "keep the durable experience";
  retained_experience.trigger_condition = "still relevant";
  retained_experience.recommended_action = "preserve it";
  retained_experience.created_at = now_millis - 1200;
  retained_experience.expires_at = now_millis + 60000;
  assert_true(store->insert_experience(retained_experience).ok,
              "maintenance retention test should persist the retained experience");

  dasall::contracts::ExperienceMemory stale_experience;
  stale_experience.experience_id = "experience-retention-stale";
  stale_experience.session_id = "session-024-retention";
  stale_experience.lesson_summary = "drop the stale experience";
  stale_experience.trigger_condition = "already expired";
  stale_experience.recommended_action = "remove it";
  stale_experience.created_at = now_millis - 5000;
  stale_experience.expires_at = now_millis - 500;
  stale_experience.superseded_by_experience_id = "experience-retention-next";
  assert_true(store->insert_experience(stale_experience).ok,
              "maintenance retention test should persist the stale experience");

  assert_true(store->quarantine_record("turn", "quarantine-retention-001", "expired").ok,
              "maintenance retention test should persist a quarantine record");
  execute_sql(database_path,
              "UPDATE quarantined_records SET created_at = 1 "
              "WHERE object_id = 'quarantine-retention-001'");

  dasall::memory::MemoryMaintenanceWorker worker(*store, config);
  const auto report = worker.execute(dasall::memory::MaintenanceRequest{});

  assert_equal(1, report.turns_purged,
               "maintenance retention should purge exactly one unprotected turn");
  assert_equal(1, report.facts_purged,
               "maintenance retention should purge one superseded stale fact");
  assert_equal(1, report.experiences_purged,
               "maintenance retention should purge one superseded expired experience");
  assert_equal(1, report.quarantine_cleaned,
               "maintenance retention should clean one expired quarantine record");

  const auto bundle = store->load_session_bundle(dasall::memory::SessionLoadRequest{
      .session_id = "session-024-retention", .recent_turn_limit = 10});
  assert_true(bundle.session.turn_ids.has_value() && bundle.session.turn_ids->size() == 2U,
              "maintenance retention should keep exactly two turn ids in the session index");
  assert_true((*bundle.session.turn_ids)[0] == "turn-retention-001" &&
                  (*bundle.session.turn_ids)[1] == "turn-retention-003",
              "maintenance retention should preserve protected and newest turns");

  assert_equal(2,
               query_scalar_count(database_path,
                                  "SELECT COUNT(*) FROM turns WHERE session_id = 'session-024-retention'"),
               "maintenance retention should leave two turn rows in sqlite");
  assert_equal(1,
               query_scalar_count(database_path,
                                  "SELECT COUNT(*) FROM facts WHERE session_id = 'session-024-retention'"),
               "maintenance retention should leave one fact row in sqlite");
  assert_equal(1,
               query_scalar_count(database_path,
                                  "SELECT COUNT(*) FROM experiences WHERE session_id = 'session-024-retention'"),
               "maintenance retention should leave one experience row in sqlite");
  assert_equal(0,
               query_scalar_count(database_path,
                                  "SELECT COUNT(*) FROM quarantined_records"),
               "maintenance retention should remove the expired quarantine row");

  store->close();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_maintenance_worker_applies_retention_and_cleanup();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}