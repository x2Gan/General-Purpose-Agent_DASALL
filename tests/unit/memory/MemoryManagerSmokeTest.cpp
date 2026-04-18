#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for sqlite smoke coverage
#endif

#include "IMemoryManager.h"
#include "support/TestAssertions.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-manager-smoke-" + std::to_string(timestamp) + ".db");
}

void test_memory_manager_factory_bootstraps_sqlite_store_after_mem_todo_014() {
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.storage.backend = "sqlite";
  config.storage.db_path = make_temp_database_path().string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  auto manager = dasall::memory::create_memory_manager(config);

  const auto init_code = manager->init(config);

  assert_true(static_cast<int>(init_code) == 0,
              "sqlite-backed memory manager should initialize once the sqlite store lands");

  manager->shutdown();
}

void test_memory_manager_smoke_surface_covers_writeback_export_and_maintenance() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryConfig config;
  config.storage.backend = "memory";

  auto manager = dasall::memory::create_memory_manager(config);
  assert_equal(0, static_cast<int>(manager->init(config)),
               "memory backend bootstrap should initialize for smoke coverage");

  dasall::memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = "session-001";
  writeback_request.turn.turn_id = "turn-001";
  writeback_request.turn.session_id = "session-001";
  writeback_request.turn.user_input = "Remember the last command result";

  const auto writeback_result = manager->write_back(writeback_request);
  assert_true(writeback_result.result_code ==
                  dasall::contracts::ResultCode::RuntimeRetryExhausted,
              "write_back should degrade cleanly until the writeback pipeline lands");
  assert_true(writeback_result.degraded,
              "write_back should mark the unwired pipeline as degraded");
  assert_true(writeback_result.warnings.size() == 1U &&
                  writeback_result.warnings.front() == "writeback_pipeline_unwired",
              "write_back should surface the pipeline-unwired warning");

  dasall::memory::WorkingMemoryExportRequest export_request;
  export_request.session_id = "session-001";
  export_request.export_reason = "checkpoint";
  export_request.include_ephemeral_facts = true;

  const auto export_result = manager->export_working_memory_snapshot(export_request);
  assert_true(!export_result.result_code.has_value(),
          "working-memory export should stay successful after the board lands");
  assert_true(export_result.snapshot.session_id == "session-001",
              "working-memory export should preserve the target session id");
    assert_true(!export_result.degraded,
          "working-memory export should no longer report degraded execution once the board is wired");
    assert_true(export_result.snapshot.slots.empty(),
          "working-memory export should return an empty slot set for a missing session");
    assert_true(export_result.warnings.size() == 1U &&
            export_result.warnings.front() == "session_not_found",
          "working-memory export should surface a missing-session warning when no snapshot exists");

  const auto maintenance_report = manager->run_maintenance(dasall::memory::MaintenanceRequest{});
  assert_true(maintenance_report.warnings.size() == 1U &&
                  maintenance_report.warnings.front() == "maintenance_worker_unwired",
              "maintenance should degrade cleanly until the maintenance worker lands");

  manager->shutdown();
}

}  // namespace

int main() {
  try {
    test_memory_manager_factory_bootstraps_sqlite_store_after_mem_todo_014();
    test_memory_manager_smoke_surface_covers_writeback_export_and_maintenance();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
