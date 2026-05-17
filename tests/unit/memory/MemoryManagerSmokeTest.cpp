#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for sqlite smoke coverage
#endif

#include "IMemoryManager.h"
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

[[nodiscard]] bool snapshot_has_slot(
    const dasall::memory::WorkingMemorySnapshot& snapshot,
    const std::string& key,
    const std::string& expected_value) {
  return std::any_of(snapshot.slots.begin(), snapshot.slots.end(),
                     [&key, &expected_value](const dasall::memory::WorkingMemorySlot& slot) {
                       return slot.key == key && slot.value == expected_value;
                     });
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_smoke_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& summary_text) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = summary_text;
  request.summary_candidate->confirmed_facts =
      std::vector<std::string>{"memory smoke summary fact"};
  return request;
}

void test_memory_manager_factory_bootstraps_sqlite_store_after_mem_todo_014() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path("dasall-memory-manager-smoke-bootstrap");
  cleanup_database_artifacts(database_path);

  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  auto manager = dasall::memory::create_memory_manager(config);

  const auto init_code = manager->init(config);

  assert_true(static_cast<int>(init_code) == 0,
              "sqlite-backed memory manager should initialize once the sqlite store lands");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_memory_manager_smoke_surface_covers_sqlite_writeback_export_and_maintenance() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path("dasall-memory-manager-smoke-surface");
  cleanup_database_artifacts(database_path);

  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.vector.enabled = true;
  config.vector.backend_type = dasall::memory::VectorBackend::SqliteVss;

  auto manager = dasall::memory::create_memory_manager(config);
  assert_equal(0, static_cast<int>(manager->init(config)),
               "sqlite backend should initialize for smoke coverage");

  const auto writeback_result = manager->write_back(
      make_smoke_request("session-026-smoke", "turn-026-smoke-001",
                         "记录 memory smoke gate 状态",
                         "已接线 sqlite writeback/export/maintenance 主链",
                         "memory smoke summary"));
  assert_true(!writeback_result.result_code.has_value(),
              "write_back should succeed once the sqlite writeback pipeline is wired");
  assert_true(writeback_result.persisted_turn_id ==
                  std::optional<std::string>{"turn-026-smoke-001"},
              "write_back should report the persisted turn id on the sqlite smoke path");
  assert_true(writeback_result.summary_id.has_value(),
              "write_back should materialize a summary id on the sqlite smoke path");

  dasall::memory::WorkingMemoryExportRequest export_request;
  export_request.session_id = "session-026-smoke";
  export_request.export_reason = "smoke-gate";
  export_request.include_ephemeral_facts = true;

  const auto export_result = manager->export_working_memory_snapshot(export_request);
  assert_true(!export_result.result_code.has_value(),
              "working-memory export should stay successful on the sqlite smoke path");
  assert_true(export_result.snapshot.session_id == "session-026-smoke",
              "working-memory export should preserve the target session id");
  assert_true(snapshot_has_slot(export_result.snapshot, "latest_turn_id",
                                "turn-026-smoke-001"),
              "working-memory export should reflect the latest turn id after sqlite writeback");
  assert_true(snapshot_has_slot(export_result.snapshot, "latest_summary_id",
                                *writeback_result.summary_id),
              "working-memory export should reflect the latest summary id after sqlite writeback");
  assert_true(!export_result.degraded,
              "working-memory export should remain non-degraded once the board is wired");
  assert_true(std::find(export_result.warnings.begin(), export_result.warnings.end(),
                        "session_not_found") == export_result.warnings.end(),
              "working-memory export should no longer surface a missing-session warning once writeback has updated the board");

    const auto context_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
        .request_id = "req-026-smoke-vector",
        .session_id = "session-026-smoke",
        .stage = "reasoning",
        .goal_summary = "验证 vector fail-closed 不阻塞主链",
        .token_budget_hint = 128,
      });
    assert_true(std::find(context_result.warnings.begin(), context_result.warnings.end(),
              "vector_unavailable") != context_result.warnings.end(),
          "vector-enabled smoke path should fail closed with vector_unavailable when sqlite-vss is not yet available");

  dasall::memory::MaintenanceRequest request;
  request.run_checkpoint = false;
  request.run_retention = false;
  request.run_quarantine_cleanup = true;
  request.run_vector_rebuild = false;
  const auto maintenance_report = manager->run_maintenance(request);
  assert_true(std::find(maintenance_report.warnings.begin(),
                        maintenance_report.warnings.end(),
                        "maintenance_worker_unwired") == maintenance_report.warnings.end(),
              "maintenance should execute through the sqlite maintenance worker on the smoke path");
  assert_equal(0, maintenance_report.quarantine_cleaned,
               "maintenance smoke coverage should stay idle when no quarantine rows are present");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_manager_factory_bootstraps_sqlite_store_after_mem_todo_014();
    test_memory_manager_smoke_surface_covers_sqlite_writeback_export_and_maintenance();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
