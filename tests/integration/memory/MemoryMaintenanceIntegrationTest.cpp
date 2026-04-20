#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "IMemoryManager.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

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

void execute_sql(const std::filesystem::path& database_path, const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for maintenance integration test");
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
  config.maintenance.quarantine_ttl_ms = 1000;
  config.maintenance.retention_turns = 16;
  return config;
}

void seed_old_quarantine_record(const std::filesystem::path& database_path,
                                const std::string& object_id) {
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_sqlite_config(database_path);
  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to open sqlite store for quarantine seeding");
  }

  if (!store->quarantine_record("turn", object_id, "maintenance-seed").ok) {
    throw std::runtime_error("failed to seed quarantine record");
  }
  store->close();

  execute_sql(database_path,
              "UPDATE quarantined_records SET created_at = 1 WHERE object_id = '" +
                  object_id + "'");
}

bool wait_for_quarantine_cleanup(const std::filesystem::path& database_path,
                                 const std::string& object_id) {
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (query_scalar_count(database_path,
                           "SELECT COUNT(*) FROM quarantined_records WHERE object_id = '" +
                               object_id + "'") == 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  return false;
}

void test_memory_manager_run_maintenance_executes_sqlite_worker() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path("dasall-memory-maintenance-integration");
  cleanup_database_artifacts(database_path);
  seed_old_quarantine_record(database_path, "integration-quarantine-manual");

  auto config = make_sqlite_config(database_path);
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error(
        "DASALL_SQL_MEMORY_DIR must be defined for maintenance integration coverage");
  }

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_equal(0, static_cast<int>(init_code),
               "sqlite-backed memory manager should initialize for maintenance coverage");

  dasall::memory::MaintenanceRequest request;
  request.run_checkpoint = false;
  request.run_retention = false;
  request.run_quarantine_cleanup = true;
  request.run_vector_rebuild = false;
  const auto report = manager->run_maintenance(request);

  assert_equal(1, report.quarantine_cleaned,
               "run_maintenance should clean the seeded quarantine row");
  assert_true(std::find(report.warnings.begin(), report.warnings.end(),
                        "maintenance_worker_unwired") == report.warnings.end(),
              "run_maintenance should be wired to the sqlite maintenance worker");
  assert_equal(0,
               query_scalar_count(database_path,
                                  "SELECT COUNT(*) FROM quarantined_records WHERE object_id = 'integration-quarantine-manual'"),
               "run_maintenance should persist cleanup effects into sqlite");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_memory_manager_auto_schedule_runs_background_maintenance() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path("dasall-memory-maintenance-auto");
  cleanup_database_artifacts(database_path);
  seed_old_quarantine_record(database_path, "integration-quarantine-auto");

  auto config = make_sqlite_config(database_path);
  config.maintenance.auto_schedule = true;
  config.maintenance.schedule_interval_ms = 20;

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_equal(0, static_cast<int>(init_code),
               "sqlite-backed memory manager should initialize with auto schedule enabled");

  assert_true(wait_for_quarantine_cleanup(database_path, "integration-quarantine-auto"),
              "auto-scheduled maintenance should eventually clean the seeded quarantine row");

  manager->shutdown();
  assert_equal(0,
               query_scalar_count(database_path,
                                  "SELECT COUNT(*) FROM quarantined_records WHERE object_id = 'integration-quarantine-auto'"),
               "auto-scheduled maintenance should leave no matching quarantine row behind");
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_manager_run_maintenance_executes_sqlite_worker();
    test_memory_manager_auto_schedule_runs_background_maintenance();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}