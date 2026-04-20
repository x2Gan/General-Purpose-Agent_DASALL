#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "maintenance/MemoryMaintenanceWorker.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

class SpyVectorMemoryIndexAdapter final : public dasall::memory::VectorMemoryIndexAdapter {
 public:
  [[nodiscard]] bool is_available() const override {
    return true;
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      const dasall::memory::VectorDocument& doc) override {
    (void)doc;
    return dasall::memory::StoreResult::success();
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      const std::string& query_text,
      int top_k) override {
    (void)query_text;
    (void)top_k;
    return {};
  }

  [[nodiscard]] dasall::memory::VectorIndexHealth health() const override {
    return dasall::memory::VectorIndexHealth{
        .available = true,
        .indexed_doc_count = 0,
        .last_rebuild_at = 0,
        .backend_type = "spy",
    };
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index() override {
    ++rebuild_call_count;
    last_rebuild_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    return dasall::memory::StoreResult::success("vector-rebuild");
  }

  int rebuild_call_count = 0;
  std::int64_t last_rebuild_at = 0;
};

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-maintenance-checkpoint-" +
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

dasall::memory::MemoryConfig make_sqlite_config(const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = "sqlite";
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.maintenance.retention_turns = 32;
  config.maintenance.quarantine_ttl_ms = 60000;
  return config;
}

void test_memory_maintenance_worker_runs_passive_checkpoint_and_vector_rebuild() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_sqlite_config(database_path);
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error(
        "DASALL_SQL_MEMORY_DIR must be defined for maintenance checkpoint coverage");
  }

  assert_true(!store->open(config).has_value(),
              "sqlite store should open for maintenance checkpoint coverage");

  dasall::contracts::Session session;
  session.session_id = "session-024-checkpoint";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = current_time_millis() - 1000;
  assert_true(store->create_session(session).ok,
              "maintenance checkpoint test should create a session");

  dasall::contracts::Turn turn;
  turn.turn_id = "turn-checkpoint-001";
  turn.session_id = "session-024-checkpoint";
  turn.user_input = "checkpoint coverage";
  turn.agent_response = "checkpoint path should execute";
  turn.created_at = current_time_millis();
  assert_true(store->append_turn(turn).ok,
              "maintenance checkpoint test should append a turn to create WAL activity");

  SpyVectorMemoryIndexAdapter vector_adapter;
  dasall::memory::MemoryMaintenanceWorker worker(*store, config, &vector_adapter);
  dasall::memory::MaintenanceRequest request;
  request.run_checkpoint = true;
  request.run_retention = false;
  request.run_quarantine_cleanup = false;
  request.run_vector_rebuild = true;

  const auto report = worker.execute(request);

  assert_true(report.checkpoint_executed,
              "maintenance checkpoint test should execute a passive WAL checkpoint");
  assert_true(report.checkpoint_wal_pages_remaining >= 0,
              "maintenance checkpoint test should surface remaining WAL pages");
  assert_true(report.vector_rebuild_executed,
              "maintenance checkpoint test should mark vector rebuild execution");
  assert_true(vector_adapter.rebuild_call_count == 1 && vector_adapter.last_rebuild_at > 0,
              "maintenance checkpoint test should invoke vector rebuild exactly once");

  store->close();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_maintenance_worker_runs_passive_checkpoint_and_vector_rebuild();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}