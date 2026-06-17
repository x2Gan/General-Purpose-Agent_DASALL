#include <chrono>
#include <exception>
#include <filesystem>
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

std::int64_t query_int64(sqlite3* connection, const std::string& sql) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("failed to prepare int64 query");
  }

  std::int64_t value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int64(statement, 0);
  }
  sqlite3_finalize(statement);
  return value;
}

bool column_exists(sqlite3* connection,
                   const std::string& table_name,
                   const std::string& column_name) {
  sqlite3_stmt* statement = nullptr;
  const auto pragma = "PRAGMA table_info(" + table_name + ")";
  if (sqlite3_prepare_v2(connection, pragma.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
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

std::filesystem::path make_temp_migration_dir(const std::string& suffix) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("dasall-memory-v004-migrations-" + suffix + "-" +
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

void exec_sql(sqlite3* connection, const std::string& sql) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message = error_message == nullptr
                                    ? "failed to execute sqlite statement"
                                    : error_message;
    sqlite3_free(error_message);
    throw std::runtime_error(message);
  }
}

void test_schema_migration_v004_adds_decay_metadata_on_fresh_database() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedSqliteConnection connection(":memory:");
  const dasall::memory::store::sqlite::SqliteSchemaMigrator migrator(DASALL_SQL_MEMORY_DIR);

  const auto migration_result = migrator.migrate(connection.get());
  assert_true(!migration_result.has_value(),
              "V004 migration should apply cleanly on a fresh database");
  assert_true(column_exists(connection.get(), "facts", "last_accessed_at"),
              "V004 migration should add facts.last_accessed_at");
  assert_true(column_exists(connection.get(), "facts", "hit_count"),
              "V004 migration should add facts.hit_count");
  assert_true(column_exists(connection.get(), "experiences", "last_accessed_at"),
              "V004 migration should add experiences.last_accessed_at");
  assert_true(column_exists(connection.get(), "experiences", "hit_count"),
              "V004 migration should add experiences.hit_count");
  assert_equal(4, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "fresh V004 migrate should record four bundled migration rows");

  const auto status = migrator.status(connection.get());
  assert_equal(4, status.current_version,
               "fresh V004 migrate should report current_version=4");
  assert_equal(4, status.target_version,
               "fresh V004 migrate should report target_version=4");
  assert_true(status.up_to_date,
              "fresh V004 migrate should leave the database up-to-date");
}

void test_schema_migration_v004_backfills_existing_rows_on_v003_upgrade() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto v003_dir = make_temp_migration_dir("baseline");
  copy_migration_file(v003_dir, "V001__initial_schema.sql");
  copy_migration_file(v003_dir, "V002__vector_sidecar.sql");
  copy_migration_file(v003_dir, "V003__fact_user_lookup_index.sql");

  ScopedSqliteConnection connection(":memory:");
  const dasall::memory::store::sqlite::SqliteSchemaMigrator v003_migrator(
      v003_dir.string());
  const auto baseline_result = v003_migrator.migrate(connection.get());
  assert_true(!baseline_result.has_value(),
              "V003 baseline migrations should apply before the V004 upgrade check");
  assert_equal(3, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "V003 baseline should only record the first three migrations");

  exec_sql(connection.get(),
           "INSERT INTO facts(fact_id, session_id, user_id, fact_text, source_turn_ids_json, confidence_score, fact_type, validity_ref, evidence_digest, superseded_by_fact_id, created_at, tags_json) "
           "VALUES('fact-v004-upgrade', NULL, 'user-v004', 'fact before V004', '[]', 80, 'preference', NULL, NULL, NULL, 123456, '[]')");
  exec_sql(connection.get(),
           "INSERT INTO experiences(experience_id, session_id, user_id, lesson_summary, trigger_condition, recommended_action, source_turn_ids_json, effectiveness_score, applicable_domains_json, risk_notes_json, expires_at, superseded_by_experience_id, created_at, tags_json) "
           "VALUES('experience-v004-upgrade', NULL, 'user-v004', 'experience before V004', 'upgrade', 'backfill metadata', '[]', 70, '[]', '[]', NULL, NULL, 234567, '[]')");

  const dasall::memory::store::sqlite::SqliteSchemaMigrator full_migrator(
      DASALL_SQL_MEMORY_DIR);
  const auto upgrade_result = full_migrator.migrate(connection.get());
  assert_true(!upgrade_result.has_value(),
              "re-running migrate against bundled migrations should apply only V004");
  assert_equal(4, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "V004 upgrade should append one migration row to the V003 baseline");
  assert_true(column_exists(connection.get(), "facts", "last_accessed_at"),
              "V004 upgrade should add facts.last_accessed_at");
  assert_true(column_exists(connection.get(), "facts", "hit_count"),
              "V004 upgrade should add facts.hit_count");
  assert_true(column_exists(connection.get(), "experiences", "last_accessed_at"),
              "V004 upgrade should add experiences.last_accessed_at");
  assert_true(column_exists(connection.get(), "experiences", "hit_count"),
              "V004 upgrade should add experiences.hit_count");
  assert_equal(std::int64_t{123456},
               query_int64(connection.get(),
                           "SELECT last_accessed_at FROM facts WHERE fact_id = 'fact-v004-upgrade'"),
               "V004 upgrade should backfill facts.last_accessed_at from created_at");
  assert_equal(std::int64_t{1},
               query_int64(connection.get(),
                           "SELECT hit_count FROM facts WHERE fact_id = 'fact-v004-upgrade'"),
               "V004 upgrade should initialize facts.hit_count to one");
  assert_equal(std::int64_t{234567},
               query_int64(connection.get(),
                           "SELECT last_accessed_at FROM experiences WHERE experience_id = 'experience-v004-upgrade'"),
               "V004 upgrade should backfill experiences.last_accessed_at from created_at");
  assert_equal(std::int64_t{1},
               query_int64(connection.get(),
                           "SELECT hit_count FROM experiences WHERE experience_id = 'experience-v004-upgrade'"),
               "V004 upgrade should initialize experiences.hit_count to one");

  std::filesystem::remove_all(v003_dir);
}

}  // namespace

int main() {
  try {
    test_schema_migration_v004_adds_decay_metadata_on_fresh_database();
    test_schema_migration_v004_backfills_existing_rows_on_v003_upgrade();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}