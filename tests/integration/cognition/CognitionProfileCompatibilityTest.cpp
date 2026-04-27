#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition profile compatibility coverage
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

struct ProfileCase {
  std::string profile_id;
  bool allow_partially_completed;
};

void test_cognition_profile_compatibility_matrix() {
  const std::vector<ProfileCase> profile_cases = {
      {"desktop_full", false},
      {"cloud_full", false},
      {"edge_balanced", true},
      {"edge_minimal", true},
      {"factory_test", true},
  };

  for (const auto& profile_case : profile_cases) {
    const auto database_path =
        make_temp_database_path("dasall-cognition-profile-" + profile_case.profile_id);
    cleanup_database_artifacts(database_path);

    const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
    auto dependency_set = make_true_integration_dependency_set(
        config,
        "session-029-" + profile_case.profile_id,
        "turn-029-" + profile_case.profile_id + "-001",
        "query profile compatibility " + profile_case.profile_id);

    dasall::runtime::AgentFacade facade;
    const auto init_result = facade.init(make_true_integration_init_request(
        dependency_set,
        "rt-029-" + profile_case.profile_id,
        profile_case.profile_id,
        "cognition-profile-compatibility-" + profile_case.profile_id));
    assert_true(init_result.accepted,
                "profile compatibility case should initialize AgentFacade: " +
                    profile_case.profile_id);

    const auto result = facade.handle(make_agent_request(
        "req-029-" + profile_case.profile_id,
        "session-029-" + profile_case.profile_id,
        "trace-029-" + profile_case.profile_id,
        "query profile compatibility " + profile_case.profile_id));

    const auto completed = result.status == dasall::contracts::AgentResultStatus::Completed;
    const auto partially_completed =
        result.status == dasall::contracts::AgentResultStatus::PartiallyCompleted;

    assert_true(completed || (profile_case.allow_partially_completed && partially_completed),
                "profile compatibility should return a deterministic terminal status: " +
                    profile_case.profile_id);
    assert_true(result.response_text.has_value() && !result.response_text->empty(),
                "profile compatibility should produce non-empty response_text: " +
                    profile_case.profile_id);
    assert_true(result.goal_id.has_value() && !result.goal_id->empty(),
                "profile compatibility should preserve goal id anchor: " +
                    profile_case.profile_id);

    if (dependency_set->memory_manager != nullptr) {
      dependency_set->memory_manager->shutdown();
    }
    cleanup_database_artifacts(database_path);
  }
}

}  // namespace

int main() {
  try {
    test_cognition_profile_compatibility_matrix();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
