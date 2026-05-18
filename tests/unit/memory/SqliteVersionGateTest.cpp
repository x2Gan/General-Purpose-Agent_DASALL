#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

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

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  return config;
}

void test_sqlite_version_gate_defaults_to_wal_reset_fixed_baseline() {
  using dasall::tests::support::assert_equal;

  const dasall::memory::MemoryConfig config;
  assert_equal(dasall::memory::encode_sqlite_version_number(3, 51, 3),
               config.storage.sqlite_min_version,
               "sqlite version gate should default to the 3.51.3 WAL-reset fixed baseline");
}

void test_sqlite_version_gate_allows_backport_floor_override() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-sqlite-version-gate-backport");
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  auto config = make_sqlite_config(database_path);
  config.storage.sqlite_min_version =
      dasall::memory::encode_sqlite_version_number(3, 50, 7);

  assert_true(!store->open(config).has_value(),
              "sqlite version gate should allow configured backport floors when the runtime is newer");

  store->close();
  cleanup_database_artifacts(database_path);
}

void test_sqlite_version_gate_rejects_runtime_below_minimum() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-sqlite-version-gate-reject");
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  auto config = make_sqlite_config(database_path);
  config.storage.sqlite_min_version = sqlite3_libversion_number() + 1;

  const auto open_result = store->open(config);
  assert_true(open_result.has_value(),
              "sqlite version gate should reject runtimes below the configured minimum");
  assert_true(*open_result ==
                  dasall::memory::map_memory_error(
                      dasall::memory::MemoryError::ConfigInvalid)
                      .result_code,
              "sqlite version gate should surface ConfigInvalid through the stable result code");
  assert_true(!std::filesystem::exists(database_path),
              "sqlite version gate should fail before touching the database file on disk");
}

}  // namespace

int main() {
  try {
    test_sqlite_version_gate_defaults_to_wal_reset_fixed_baseline();
    test_sqlite_version_gate_allows_backport_floor_override();
    test_sqlite_version_gate_rejects_runtime_below_minimum();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}