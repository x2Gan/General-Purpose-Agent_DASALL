#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "store/sqlite/SqliteSchemaMigrator.h"
#include "support/TestAssertions.h"

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR "/home/gangan/DASALL/sql/memory"
#endif

namespace {

class ScopedSqliteConnection {
 public:
  explicit ScopedSqliteConnection(const std::string& database_path) {
    if (sqlite3_open(database_path.c_str(), &connection_) != SQLITE_OK) {
      throw std::runtime_error("failed to open sqlite connection");
    }
  }

  ~ScopedSqliteConnection() {
    if (connection_ != nullptr) {
      sqlite3_close(connection_);
    }
  }

  ScopedSqliteConnection(const ScopedSqliteConnection&) = delete;
  ScopedSqliteConnection& operator=(const ScopedSqliteConnection&) = delete;

  [[nodiscard]] sqlite3* get() const {
    return connection_;
  }

 private:
  sqlite3* connection_ = nullptr;
};

int query_count(sqlite3* connection, const std::string& sql) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare count query");
  }

  int value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return value;
}

bool table_exists(sqlite3* connection, const std::string& table_name) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?1";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare table-exists query");
  }

  sqlite3_bind_text(statement, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
  bool exists = false;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    exists = sqlite3_column_int(statement, 0) == 1;
  }
  sqlite3_finalize(statement);
  return exists;
}

std::filesystem::path make_temp_migration_dir(const std::string& suffix) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("dasall-memory-migrations-" + suffix + "-" +
                          std::to_string(timestamp));
  std::filesystem::create_directories(directory);
  return directory;
}

void write_migration_file(const std::filesystem::path& directory,
                          const std::string& filename,
                          const std::string& content) {
  std::ofstream stream(directory / filename, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to write migration file");
  }
  stream << content;
}

void test_sqlite_schema_migrator_applies_v001_and_reports_up_to_date_status() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedSqliteConnection connection(":memory:");
  const dasall::memory::store::sqlite::SqliteSchemaMigrator migrator(DASALL_SQL_MEMORY_DIR);

  const auto first_result = migrator.migrate(connection.get());
  assert_true(!first_result.has_value(),
              "schema migrator should apply the bundled V001 schema to a fresh database");
  assert_true(table_exists(connection.get(), "sessions"),
              "V001 schema should create the sessions table");
  assert_true(table_exists(connection.get(), "turns"),
              "V001 schema should create the turns table");
  assert_true(table_exists(connection.get(), "summaries"),
              "V001 schema should create the summaries table");
  assert_true(table_exists(connection.get(), "facts"),
              "V001 schema should create the facts table");
  assert_true(table_exists(connection.get(), "experiences"),
              "V001 schema should create the experiences table");
  assert_true(table_exists(connection.get(), "memory_vector_documents"),
              "V002 schema should create the vector sidecar table");
  assert_true(table_exists(connection.get(), "quarantined_records"),
              "V001 schema should create the quarantine table");
  assert_equal(2, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "fresh database should record both bundled migrations after V002");

  const auto migration_status = migrator.status(connection.get());
  assert_equal(2, migration_status.current_version,
               "migration status should report the applied V002 version");
  assert_equal(2, migration_status.target_version,
               "migration status should report V002 as the current target version");
  assert_true(migration_status.up_to_date,
              "migration status should report the database as up-to-date after V002");

  const auto second_result = migrator.migrate(connection.get());
  assert_true(!second_result.has_value(),
              "schema migrator should be a no-op when the database is already up-to-date");
  assert_equal(2, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "re-running migrate should not duplicate applied migration rows");
}

void test_sqlite_schema_migrator_rejects_checksum_mismatch() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto migration_dir = make_temp_migration_dir("checksum");
  write_migration_file(migration_dir, "V001__initial_schema.sql",
                       "CREATE TABLE sample_table(id INTEGER PRIMARY KEY, name TEXT);\n");

  ScopedSqliteConnection connection(":memory:");
  {
    const dasall::memory::store::sqlite::SqliteSchemaMigrator migrator(
        migration_dir.string());
    const auto first_result = migrator.migrate(connection.get());
    assert_true(!first_result.has_value(),
                "schema migrator should apply the initial custom migration successfully");
  }

  write_migration_file(migration_dir, "V001__initial_schema.sql",
                       "CREATE TABLE sample_table(id INTEGER PRIMARY KEY, name TEXT, tag TEXT);\n");

  const dasall::memory::store::sqlite::SqliteSchemaMigrator mismatched_migrator(
      migration_dir.string());
  const auto mismatch_result = mismatched_migrator.migrate(connection.get());
  assert_true(mismatch_result ==
                  dasall::contracts::ResultCode::ValidationFieldMissing,
              "schema migrator should reject checksum drift for an already-applied migration");
  assert_equal(1, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "checksum mismatch should not duplicate migration metadata rows");

  std::filesystem::remove_all(migration_dir);
}

}  // namespace

int main() {
  try {
    test_sqlite_schema_migrator_applies_v001_and_reports_up_to_date_status();
    test_sqlite_schema_migrator_rejects_checksum_mismatch();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
