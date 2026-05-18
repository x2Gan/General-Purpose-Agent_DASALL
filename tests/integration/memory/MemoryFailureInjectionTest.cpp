#include <algorithm>
#include <chrono>
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
#include "error/MemoryError.h"
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

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] bool contains_warning(const std::vector<std::string>& warnings,
                                    const std::string& warning) {
  return std::find(warnings.begin(), warnings.end(), warning) != warnings.end();
}

void execute_sql(const std::filesystem::path& database_path, const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for failure injection setup");
  }

  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message = error_message == nullptr
                                    ? "failed to execute sqlite failure injection statement"
                                    : error_message;
    sqlite3_free(error_message);
    sqlite3_close(connection);
    throw std::runtime_error(message);
  }

  sqlite3_close(connection);
}

void execute_sql(sqlite3* connection, const std::string& sql) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message = error_message == nullptr
                                    ? "failed to execute sqlite statement on writer connection"
                                    : error_message;
    sqlite3_free(error_message);
    throw std::runtime_error(message);
  }
}

int query_scalar_int(sqlite3* connection, const std::string& sql) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare sqlite scalar query for failure injection");
  }

  int value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }

  sqlite3_finalize(statement);
  return value;
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path,
    dasall::memory::JournalMode journal_mode = dasall::memory::JournalMode::Wal) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.storage.journal_mode = journal_mode;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_valid_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& summary_text) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = "record failure injection gate";
  request.turn.agent_response = "verify memory failure path evidence";
  request.turn.created_at = current_time_millis();
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = summary_text;
  request.summary_candidate->confirmed_facts =
      std::vector<std::string>{"failure injection baseline"};
  return request;
}

void test_memory_manager_init_reports_config_invalid_when_sqlite_runtime_is_below_minimum() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-sqlite-version-gate-injection");
  cleanup_database_artifacts(database_path);

  auto config = make_sqlite_config(database_path);
  config.storage.sqlite_min_version = sqlite3_libversion_number() + 1;

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(init_code ==
                  dasall::memory::map_memory_error(
                      dasall::memory::MemoryError::ConfigInvalid)
                      .result_code,
              "sqlite version gate injection should surface ConfigInvalid during init");
  assert_true(!std::filesystem::exists(database_path),
              "sqlite version gate injection should fail before creating the database file");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_memory_manager_init_reports_schema_mismatch_after_checksum_tamper() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-schema-mismatch-injection");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(config);
  assert_true(static_cast<int>(manager->init(config)) == 0,
              "schema mismatch setup should initialize the sqlite-backed memory manager once");
  manager->shutdown();

  execute_sql(database_path,
              "UPDATE schema_migrations SET checksum = 'tampered' WHERE version = 1");

  auto mismatched_manager = dasall::memory::create_memory_manager(config);
  const auto init_code = mismatched_manager->init(config);
  assert_true(init_code ==
                  dasall::memory::map_memory_error(
                      dasall::memory::MemoryError::SchemaMismatch)
                      .result_code,
              "schema mismatch injection should surface the schema mismatch result code during init");
  assert_true(dasall::memory::map_memory_error(
                  dasall::memory::MemoryError::SchemaMismatch)
                  .audit_required,
              "schema mismatch injection should map to an audit-required memory error");

  mismatched_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_sqlite_store_reports_storage_unavailable_when_database_is_full() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path("dasall-memory-sqlite-full");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, dasall::memory::JournalMode::Delete);
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  assert_true(!store->open(config).has_value(),
              "sqlite store should open before disk-full failure injection");

  dasall::contracts::Session session;
  session.session_id = "session-028-full";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = current_time_millis();
  assert_true(store->create_session(session).ok,
              "disk-full failure injection should create a seed session");

  auto* sqlite_store =
      dynamic_cast<dasall::memory::store::sqlite::SqliteMemoryStore*>(store.get());
  if (sqlite_store == nullptr) {
    throw std::runtime_error("failed to access sqlite store implementation for disk-full injection");
  }

  sqlite3* writer_connection = sqlite_store->writer_connection_for_maintenance();
  if (writer_connection == nullptr) {
    throw std::runtime_error("failed to access sqlite writer connection for disk-full injection");
  }

  const int current_pages =
      std::max(1, query_scalar_int(writer_connection, "PRAGMA page_count"));
  execute_sql(writer_connection,
              "PRAGMA max_page_count = " + std::to_string(current_pages));

  dasall::contracts::SummaryMemory summary;
  summary.summary_id = "summary-028-full";
  summary.session_id = "session-028-full";
  summary.summary_text = std::string(2 * 1024 * 1024, 's');
  summary.source_turn_ids = std::vector<std::string>{"turn-028-full-001"};
  summary.created_at = current_time_millis();

  const auto result = store->upsert_summary(summary);
  assert_true(!result.ok,
              "disk-full failure injection should fail the oversized summary write");
  assert_true(result.result_code ==
                  dasall::memory::map_memory_error(
                      dasall::memory::MemoryError::StorageUnavailable)
                      .result_code,
              "disk-full failure injection should surface the storage unavailable result code");
  assert_true(dasall::memory::map_memory_error(
                  dasall::memory::MemoryError::StorageUnavailable)
                  .audit_required,
              "disk-full failure injection should map to an audit-required memory error");

  store->close();
  cleanup_database_artifacts(database_path);
}

void test_memory_manager_writeback_surfaces_summary_quarantine_as_partial_warning() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-summary-quarantine");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(config);
  assert_true(static_cast<int>(manager->init(config)) == 0,
              "summary quarantine injection should initialize the sqlite-backed memory manager");

  auto request = make_valid_request("session-028-summary", "turn-028-summary-001",
                                    "valid summary before quarantine");
  request.summary_candidate->summary_text = std::nullopt;

  const auto result = manager->write_back(request);
  assert_true(!result.result_code.has_value(),
              "summary quarantine injection should keep the core writeback successful");
  assert_true(result.persisted_turn_id ==
                  std::optional<std::string>{"turn-028-summary-001"},
              "summary quarantine injection should still persist the core turn");
  assert_true(result.partial,
              "summary quarantine injection should mark the writeback as partial");
  assert_true(!result.summary_id.has_value(),
              "summary quarantine injection should drop the invalid summary candidate from the main path");
  assert_true(contains_warning(result.warnings, "summary_candidate_rejected") &&
                  contains_warning(result.warnings, "partial_writeback_warning"),
              "summary quarantine injection should surface explicit partial-write warnings");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_memory_manager_writeback_stays_healthy_when_vector_is_disabled() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path("dasall-memory-vector-off");
  cleanup_database_artifacts(database_path);

  auto config = make_sqlite_config(database_path);
  config.vector.enabled = false;

  auto manager = dasall::memory::create_memory_manager(config);
  assert_true(static_cast<int>(manager->init(config)) == 0,
              "vector-off injection should initialize the sqlite-backed memory manager");

  const auto result = manager->write_back(
      make_valid_request("session-028-vector-off", "turn-028-vector-off-001",
                         "vector disabled summary"));
  assert_true(!result.result_code.has_value(),
              "vector-off injection should keep the main writeback path healthy");
  assert_true(result.persisted_turn_id ==
                  std::optional<std::string>{"turn-028-vector-off-001"},
              "vector-off injection should still persist the core turn");
  assert_true(!contains_warning(result.warnings, "vector_sidecar_failed"),
              "vector-off injection should not emit vector sidecar failure warnings when vector is intentionally disabled");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_manager_init_reports_config_invalid_when_sqlite_runtime_is_below_minimum();
    test_memory_manager_init_reports_schema_mismatch_after_checksum_tamper();
    test_sqlite_store_reports_storage_unavailable_when_database_is_full();
    test_memory_manager_writeback_surfaces_summary_quarantine_as_partial_warning();
    test_memory_manager_writeback_stays_healthy_when_vector_is_disabled();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}