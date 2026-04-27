#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for runtime cognition loop smoke coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "RuntimeUnaryFixture.h"
#include "../../../memory/src/store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

[[nodiscard]] bool snapshot_has_slot(
    const dasall::memory::WorkingMemorySnapshot& snapshot,
    const std::string& key,
    const std::string& expected_value) {
  for (const auto& slot : snapshot.slots) {
    if (slot.key == key && slot.value == expected_value) {
      return true;
    }
  }

  return false;
}

void test_runtime_cognition_loop_smoke() {
  const auto database_path = make_temp_database_path("dasall-runtime-cognition-loop-smoke");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config, "session-026-smoke", "turn-026-smoke-001", "query runtime cognition smoke");

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set, "rt-026-smoke", "desktop_full", "runtime-cognition-loop-smoke"));
  assert_true(init_result.accepted,
              "runtime cognition loop smoke should initialize AgentFacade successfully");

  const auto result = facade.handle(make_agent_request(
      "req-026-smoke", "session-026-smoke", "trace-026-smoke", "query runtime cognition smoke"));

  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "runtime cognition loop smoke should complete through runtime->cognition handoff");
  assert_true(result.task_completed.value_or(false),
              "runtime cognition loop smoke should set task_completed=true");
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("runtime unary integration completed:") !=
                      std::string::npos,
              "runtime cognition loop smoke should include runtime+cognition completion summary");

  const auto export_result = dependency_set->memory_manager->export_working_memory_snapshot(
      dasall::memory::WorkingMemoryExportRequest{
          .session_id = "session-026-smoke",
          .export_reason = "runtime-cognition-loop-smoke",
          .include_ephemeral_facts = true,
      });
  assert_true(!export_result.result_code.has_value() &&
                  snapshot_has_slot(export_result.snapshot,
                                    "latest_turn_id",
                                    "req-026-smoke-cognition-belief"),
              "runtime cognition loop smoke should project the cognition belief hint into memory writeback");

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to reopen sqlite store for runtime cognition loop smoke verification");
  }

  dasall::memory::FactQuery fact_query;
  fact_query.session_id = "session-026-smoke";
  fact_query.exclude_superseded = false;
  const auto fact_result = store->query_facts(fact_query);
  assert_true(fact_result.total_count >= 2,
              "runtime cognition loop smoke should persist at least one additional cognition fact beyond the seeded fixture fact");
  store->close();

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_runtime_cognition_loop_smoke();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
