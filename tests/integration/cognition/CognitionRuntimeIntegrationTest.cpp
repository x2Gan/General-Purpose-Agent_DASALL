#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition runtime integration coverage
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

void test_cognition_runtime_integration_happy_path() {
  const auto database_path = make_temp_database_path("dasall-cognition-runtime-integration");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config, "session-026-integration", "turn-026-integration-001", "query cognition runtime integration");

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-026-integration",
      "desktop_full",
      "cognition-runtime-integration"));
  assert_true(init_result.accepted,
              "cognition runtime integration should initialize AgentFacade with live dependency ports");

  const auto result = facade.handle(
      make_agent_request("req-026-integration",
                         "session-026-integration",
                         "trace-026-integration",
                         "query cognition runtime integration"));

  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "cognition runtime integration should produce a completed AgentResult");
  assert_true(result.task_completed.value_or(false),
              "cognition runtime integration should mark task_completed=true");
  assert_true(result.goal_id.has_value() && !result.goal_id->empty(),
              "cognition runtime integration should preserve resolved goal anchor");
  assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
              "cognition runtime integration should materialize terminal checkpoint reference");
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("runtime unary integration completed:") !=
                      std::string::npos,
              "cognition runtime integration should include runtime+cognition completion message");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_cognition_runtime_integration_happy_path();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
