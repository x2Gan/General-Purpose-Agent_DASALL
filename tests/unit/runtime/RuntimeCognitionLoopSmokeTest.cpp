#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for runtime cognition loop smoke coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

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
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("\"dataset\":\"agent.dataset\"") !=
                      std::string::npos,
              "runtime cognition loop smoke should include builtin tool projection payload");

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
