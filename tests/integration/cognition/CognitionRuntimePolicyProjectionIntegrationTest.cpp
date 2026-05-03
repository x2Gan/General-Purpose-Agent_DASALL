#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition runtime policy projection coverage
#endif

#include "AgentFacade.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::mocks::MockLLMManager;
using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

struct ProjectionCase {
  std::string profile_id;
};

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] const dasall::llm::LLMGenerateRequest* find_request_in(
    const std::vector<dasall::llm::LLMGenerateRequest>& requests,
    std::string_view stage,
    std::string_view task_type) {
  for (const auto& request : requests) {
    if (request.stage == stage && request.task_type == task_type) {
      return &request;
    }
  }

  return nullptr;
}

[[nodiscard]] const dasall::llm::LLMGenerateRequest* find_recorded_request(
    const dasall::tests::mocks::MockLLMManager& manager,
    std::string_view stage,
    std::string_view task_type) {
  if (const auto* request = find_request_in(manager.generate_requests(), stage, task_type)) {
    return request;
  }

  return find_request_in(manager.stream_generate_requests(), stage, task_type);
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_profile_snapshot(const std::string& profile_id) {
  const dasall::profiles::ProfileCatalog catalog(repository_root() / "profiles");
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{.profile_id = profile_id});
  assert_true(result.ok(),
              "runtime policy projection integration should load the runtime snapshot from real profile assets: " +
                  profile_id);
  assert_true(result.snapshot->has_consistent_values(),
              "runtime policy projection integration should keep snapshot values consistent: " +
                  profile_id);
  return result.snapshot;
}

void assert_stage_request_matches_policy(
    const dasall::llm::LLMGenerateRequest& request,
    const dasall::profiles::RuntimePolicySnapshot& snapshot,
    std::string_view stage_name,
    const std::string& profile_id) {
  const auto& stage_routes = snapshot.model_profile().stage_routes;
  const auto expected_timeout_ms =
      static_cast<std::uint32_t>(snapshot.timeout_policy().llm.timeout_ms);
  const auto expected_output_tokens = snapshot.token_budget_policy().max_output_tokens;

  assert_true(request.request.model_route.has_value() &&
                  *request.request.model_route == stage_routes.at(std::string(stage_name)).route,
              std::string(stage_name) +
                  " route should come from the real runtime policy snapshot: " + profile_id);
  assert_true(request.request.timeout_ms.has_value() &&
                  *request.request.timeout_ms == expected_timeout_ms,
              std::string(stage_name) +
                  " deadline should come from the real runtime policy snapshot: " + profile_id);
  assert_true(request.request.max_output_tokens.has_value() &&
                  *request.request.max_output_tokens == expected_output_tokens,
              std::string(stage_name) +
                  " output budget should come from the real runtime policy snapshot: " + profile_id);
}

void test_runtime_init_projects_real_profile_snapshot_into_live_stage_requests() {
  const std::vector<ProjectionCase> profile_cases = {
      {"desktop_full"},
      {"cloud_full"},
      {"edge_balanced"},
  };

  for (const auto& profile_case : profile_cases) {
    const auto database_path =
        make_temp_database_path("dasall-cognition-runtime-policy-" + profile_case.profile_id);
    cleanup_database_artifacts(database_path);

    const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
    auto dependency_set = make_true_integration_dependency_set(
        config,
        "session-029-policy-" + profile_case.profile_id,
        "turn-029-policy-" + profile_case.profile_id + "-001",
        "query runtime policy projection " + profile_case.profile_id);

    auto mock_llm_manager = std::dynamic_pointer_cast<MockLLMManager>(dependency_set->llm_manager);
    assert_true(mock_llm_manager != nullptr,
                "runtime policy projection integration should expose MockLLMManager for request inspection: " +
                    profile_case.profile_id);
    mock_llm_manager->clear_recorded_requests();

    dasall::runtime::AgentFacade facade;
    auto init_request = make_true_integration_init_request(
        dependency_set,
        "rt-029-policy-" + profile_case.profile_id,
        profile_case.profile_id,
        "cognition-runtime-policy-projection-" + profile_case.profile_id);
    init_request.policy_snapshot = load_profile_snapshot(profile_case.profile_id);
    const auto init_result = facade.init(init_request);

    assert_true(init_result.accepted,
                "runtime policy projection integration should initialize AgentFacade: " +
                    profile_case.profile_id);
    assert_true(init_result.resolved_profile_id == profile_case.profile_id,
                "runtime policy projection integration should bind the resolved profile id: " +
                    profile_case.profile_id);
    assert_true(init_result.diagnostics == "cognition_ports=composed_from_policy_snapshot",
                "runtime policy projection integration should compose cognition ports from the real runtime policy snapshot: " +
                    profile_case.profile_id);

    const auto result = facade.handle(make_agent_request(
        "req-029-policy-" + profile_case.profile_id,
        "session-029-policy-" + profile_case.profile_id,
        "trace-029-policy-" + profile_case.profile_id,
        "query runtime policy projection " + profile_case.profile_id));

    assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
                "runtime policy projection integration should complete on the live provider-driven path: " +
                    profile_case.profile_id);
    assert_true(result.response_text.has_value() && !result.response_text->empty(),
                "runtime policy projection integration should produce non-empty response_text: " +
                    profile_case.profile_id);
    assert_true(result.goal_id.has_value() && !result.goal_id->empty(),
                "runtime policy projection integration should preserve goal anchor: " +
                    profile_case.profile_id);

    const auto* planning_request =
        find_recorded_request(*mock_llm_manager, "planning", "plan");
    const auto* execution_request =
        find_recorded_request(*mock_llm_manager, "execution", "action_decision");
    const auto* reflection_request =
        find_recorded_request(*mock_llm_manager, "reflection", "failure_analysis");

    assert_true(planning_request != nullptr,
                "runtime policy projection integration should emit a planning bridge request: " +
                    profile_case.profile_id);
    assert_true(execution_request != nullptr,
                "runtime policy projection integration should emit an execution bridge request: " +
                    profile_case.profile_id);
    assert_true(reflection_request != nullptr,
                "runtime policy projection integration should emit a reflection bridge request: " +
                    profile_case.profile_id);

    assert_stage_request_matches_policy(
        *planning_request, *init_request.policy_snapshot, "planning", profile_case.profile_id);
    assert_stage_request_matches_policy(
        *execution_request, *init_request.policy_snapshot, "execution", profile_case.profile_id);
    assert_stage_request_matches_policy(
        *reflection_request, *init_request.policy_snapshot, "reflection", profile_case.profile_id);

    if (dependency_set->memory_manager != nullptr) {
      dependency_set->memory_manager->shutdown();
    }
    cleanup_database_artifacts(database_path);
  }
}

}  // namespace

int main() {
  try {
    test_runtime_init_projects_real_profile_snapshot_into_live_stage_requests();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}