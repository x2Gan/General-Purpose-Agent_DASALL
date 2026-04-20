#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#include "store/sqlite/SqliteMemoryStore.h"
#include "store/sqlite/SqliteSchemaMigrator.h"
#include "support/TestAssertions.h"

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

std::filesystem::path make_temp_migration_dir() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("dasall-memory-transactions-" + std::to_string(timestamp));
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

void test_sqlite_schema_migrator_rolls_back_failed_migration() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto migration_dir = make_temp_migration_dir();
  write_migration_file(migration_dir, "V001__baseline.sql",
                       "CREATE TABLE baseline_table(id INTEGER PRIMARY KEY, note TEXT);\n");
  write_migration_file(migration_dir, "V002__broken_upgrade.sql",
                       "CREATE TABLE broken_table(id INTEGER PRIMARY KEY);\n"
                       "CREATE TABLE broken_table(id INTEGER PRIMARY KEY);\n");

  ScopedSqliteConnection connection(":memory:");
  const dasall::memory::store::sqlite::SqliteSchemaMigrator migrator(
      migration_dir.string());

  const auto migration_result = migrator.migrate(connection.get());
  assert_true(migration_result ==
                  dasall::contracts::ResultCode::ValidationFieldMissing,
              "schema migrator should report schema-class failure when a migration statement set is invalid");
  assert_true(table_exists(connection.get(), "baseline_table"),
              "previously committed migration tables should remain after a later migration fails");
  assert_true(!table_exists(connection.get(), "broken_table"),
              "failing migration should roll back partial DDL from its own transaction");
  assert_equal(1, query_count(connection.get(), "SELECT COUNT(*) FROM schema_migrations"),
               "only the successfully committed migration should remain recorded");

  const auto migration_status = migrator.status(connection.get());
  assert_equal(1, migration_status.current_version,
               "failed migration should leave the database at the last committed version");
  assert_equal(2, migration_status.target_version,
               "status should still report the pending V002 target after failure");
  assert_true(!migration_status.up_to_date,
              "status should report the database as stale after a failed upgrade");

  std::filesystem::remove_all(migration_dir);
}

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-sqlite-transaction-" + std::to_string(timestamp) + ".db");
}

void test_sqlite_store_transaction_rolls_back_on_scope_exit_and_commits_explicitly() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();

  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  assert_true(!store->open(config).has_value(),
              "sqlite store should open for transaction coverage");

  {
    auto transaction = store->begin_immediate();

    dasall::contracts::Session session;
    session.session_id = "session-txn-rollback";
    session.turn_ids = std::vector<std::string>{};
    session.created_at = 100;

    assert_true(store->create_session(session).ok,
                "create_session should succeed inside a pending writer transaction");
  }

  const auto rolled_back_bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = "session-txn-rollback", .recent_turn_limit = 1});
  assert_true(!rolled_back_bundle.session.session_id.has_value(),
              "transaction destructor should roll back uncommitted session writes");

  {
    auto transaction = store->begin_immediate();

    dasall::contracts::Session session;
    session.session_id = "session-txn-commit";
    session.turn_ids = std::vector<std::string>{};
    session.created_at = 200;

    assert_true(store->create_session(session).ok,
                "create_session should succeed before explicit commit");
    assert_true(!transaction->commit().has_value(),
                "explicit sqlite store commit should succeed");
  }

  const auto committed_bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = "session-txn-commit", .recent_turn_limit = 1});
  assert_true(committed_bundle.session.session_id ==
                  std::optional<std::string>{"session-txn-commit"},
              "explicit commit should persist session writes");
  assert_equal(0, committed_bundle.total_turn_count,
               "committed session should start with an empty turn count");

  store->close();
  std::filesystem::remove(database_path);
}

}  // namespace

int main() {
  try {
    test_sqlite_schema_migrator_rolls_back_failed_migration();
    test_sqlite_store_transaction_rolls_back_on_scope_exit_and_commits_explicitly();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
