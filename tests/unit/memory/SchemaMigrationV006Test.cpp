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
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error("failed to prepare count query");
  }

  int value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return value;
}

bool column_exists(sqlite3* connection,
                   const std::string& table_name,
                   const std::string& column_name) {
  sqlite3_stmt* statement = nullptr;
  const auto pragma = "PRAGMA table_info(" + table_name + ")";
  if (sqlite3_prepare_v2(connection, pragma.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error("failed to prepare table_info query");
  }

  bool exists = false;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* current_column =
        reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    if (current_column != nullptr && column_name == current_column) {
      exists = true;
      break;
    }
  }

  sqlite3_finalize(statement);
  return exists;
}

bool index_exists(sqlite3* connection, const std::string& index_name) {
  sqlite3_stmt* statement = nullptr;
  constexpr auto query =
      "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name = ?1";
  if (sqlite3_prepare_v2(connection, query, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare index-exists query");
  }

  sqlite3_bind_text(statement, 1, index_name.c_str(), -1, SQLITE_TRANSIENT);
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
                         ("dasall-memory-v006-migrations-" + suffix + "-" +
                          std::to_string(timestamp));
  std::filesystem::create_directories(directory);
  return directory;
}

void copy_migration_file(const std::filesystem::path& destination,
                         const std::string& filename) {
  const auto source = std::filesystem::path(DASALL_SQL_MEMORY_DIR) / filename;
  const auto target = destination / filename;
  if (!std::filesystem::exists(source)) {
    throw std::runtime_error("missing bundled migration file");
  }

  std::filesystem::copy_file(source, target,
                             std::filesystem::copy_options::overwrite_existing);
}

void test_schema_migration_v006_adds_summary_parent_id_on_fresh_database() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedSqliteConnection connection(":memory:");
  const dasall::memory::store::sqlite::SqliteSchemaMigrator migrator(
      DASALL_SQL_MEMORY_DIR);

  const auto migration_result = migrator.migrate(connection.get());
  assert_true(!migration_result.has_value(),
              "V006 migration should apply cleanly on a fresh database");
  assert_true(column_exists(connection.get(), "summaries", "summary_parent_id"),
              "V006 migration should add summaries.summary_parent_id");
  assert_true(index_exists(connection.get(), "idx_summaries_session_parent_created_at"),
              "V006 migration should add the session/parent lookup index");
  assert_true(index_exists(connection.get(), "idx_summaries_parent_id"),
              "V006 migration should add the direct parent lookup index");
  assert_equal(5, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "fresh V006 migrate should record five bundled migration rows");

  const auto status = migrator.status(connection.get());
  assert_equal(6, status.current_version,
               "fresh V006 migrate should report current_version=6");
  assert_equal(6, status.target_version,
               "fresh V006 migrate should report target_version=6");
  assert_true(status.up_to_date,
              "fresh V006 migrate should leave the database up-to-date");
}

void test_schema_migration_v006_upgrades_existing_v004_database() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto v004_dir = make_temp_migration_dir("baseline");
  copy_migration_file(v004_dir, "V001__initial_schema.sql");
  copy_migration_file(v004_dir, "V002__vector_sidecar.sql");
  copy_migration_file(v004_dir, "V003__fact_user_lookup_index.sql");
  copy_migration_file(v004_dir, "V004__retention_decay_metadata.sql");

  ScopedSqliteConnection connection(":memory:");
  const dasall::memory::store::sqlite::SqliteSchemaMigrator v004_migrator(
      v004_dir.string());
  const auto baseline_result = v004_migrator.migrate(connection.get());
  assert_true(!baseline_result.has_value(),
              "V004 baseline migrations should apply before the V006 upgrade check");
  assert_equal(4, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "V004 baseline should only record the first four migrations");
  assert_true(!column_exists(connection.get(), "summaries", "summary_parent_id"),
              "V004 baseline should not already contain summary_parent_id");

  const dasall::memory::store::sqlite::SqliteSchemaMigrator full_migrator(
      DASALL_SQL_MEMORY_DIR);
  const auto upgrade_result = full_migrator.migrate(connection.get());
  assert_true(!upgrade_result.has_value(),
              "re-running migrate against bundled migrations should apply the pending hierarchy migration");
  assert_equal(5, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "V006 upgrade should append one migration row to the V004 baseline");
  assert_true(column_exists(connection.get(), "summaries", "summary_parent_id"),
              "V006 upgrade should add summary_parent_id to summaries");
  assert_true(index_exists(connection.get(), "idx_summaries_session_parent_created_at"),
              "V006 upgrade should add the session/parent lookup index");
  assert_true(index_exists(connection.get(), "idx_summaries_parent_id"),
              "V006 upgrade should add the direct parent lookup index");

  std::filesystem::remove_all(v004_dir);
}

}  // namespace

int main() {
  try {
    test_schema_migration_v006_adds_summary_parent_id_on_fresh_database();
    test_schema_migration_v006_upgrades_existing_v004_database();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}