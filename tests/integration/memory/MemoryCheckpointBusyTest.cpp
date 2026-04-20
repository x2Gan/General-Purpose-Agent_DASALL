#include <algorithm>
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
         ("dasall-memory-checkpoint-busy-" + std::to_string(timestamp) + ".db");
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

[[nodiscard]] bool contains_warning(const std::vector<std::string>& warnings,
                                    const std::string& warning) {
  return std::find(warnings.begin(), warnings.end(), warning) != warnings.end();
}

dasall::memory::MemoryConfig make_sqlite_config(const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = "sqlite";
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.storage.wal_autocheckpoint_pages = 1;
  config.maintenance.retention_turns = 16;
  return config;
}

void test_memory_maintenance_checkpoint_reports_busy_reader_gap() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_sqlite_config(database_path);
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error(
        "DASALL_SQL_MEMORY_DIR must be defined for checkpoint busy coverage");
  }

  assert_true(!store->open(config).has_value(),
              "sqlite store should open for checkpoint busy coverage");

  dasall::contracts::Session session;
  session.session_id = "session-024-busy";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = current_time_millis() - 2000;
  assert_true(store->create_session(session).ok,
              "checkpoint busy test should create a session");

  dasall::contracts::Turn first_turn;
  first_turn.turn_id = "turn-busy-001";
  first_turn.session_id = "session-024-busy";
  first_turn.user_input = "prime the database";
  first_turn.created_at = current_time_millis() - 1500;
  assert_true(store->append_turn(first_turn).ok,
              "checkpoint busy test should append the first turn");

  sqlite3* reader_connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &reader_connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open reader connection for busy checkpoint coverage");
  }

  if (sqlite3_exec(reader_connection, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK) {
    sqlite3_close(reader_connection);
    throw std::runtime_error("failed to begin reader transaction for busy checkpoint coverage");
  }

  sqlite3_stmt* reader_statement = nullptr;
  if (sqlite3_prepare_v2(reader_connection,
                         "SELECT turn_id FROM turns WHERE session_id = ?1 ORDER BY created_at ASC",
                         -1, &reader_statement, nullptr) != SQLITE_OK) {
    sqlite3_exec(reader_connection, "ROLLBACK;", nullptr, nullptr, nullptr);
    sqlite3_close(reader_connection);
    throw std::runtime_error("failed to prepare reader statement for busy checkpoint coverage");
  }
  sqlite3_bind_text(reader_statement, 1, "session-024-busy", -1, SQLITE_TRANSIENT);
  (void)sqlite3_step(reader_statement);

  dasall::contracts::Turn second_turn;
  second_turn.turn_id = "turn-busy-002";
  second_turn.session_id = "session-024-busy";
  second_turn.user_input = "write after reader begins";
  second_turn.created_at = current_time_millis();
  assert_true(store->append_turn(second_turn).ok,
              "checkpoint busy test should append another turn after the reader begins");

  dasall::memory::MemoryMaintenanceWorker worker(*store, config);
  dasall::memory::MaintenanceRequest request;
  request.run_checkpoint = true;
  request.run_retention = false;
  request.run_quarantine_cleanup = false;
  request.run_vector_rebuild = false;
  const auto report = worker.execute(request);

  assert_true(report.checkpoint_executed,
              "busy checkpoint test should still execute a passive checkpoint attempt");
  assert_true(contains_warning(report.warnings, "checkpoint_busy") ||
                  report.checkpoint_wal_pages_remaining > 0,
              "busy checkpoint test should surface busy-reader pressure through warnings or remaining WAL pages");

  sqlite3_finalize(reader_statement);
  sqlite3_exec(reader_connection, "COMMIT;", nullptr, nullptr, nullptr);
  sqlite3_close(reader_connection);

  store->close();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_maintenance_checkpoint_reports_busy_reader_gap();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}