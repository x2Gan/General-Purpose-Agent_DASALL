#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition profile compatibility coverage
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

struct ProfileCase {
  std::string profile_id;
  dasall::contracts::AgentResultStatus expected_status;
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
                            "profile compatibility should load the runtime snapshot from the real profile assets: " +
                                    profile_id);
    return result.snapshot;
}

void test_cognition_profile_compatibility_matrix() {
  const std::vector<ProfileCase> profile_cases = {
      {"desktop_full", dasall::contracts::AgentResultStatus::Completed},
      {"cloud_full", dasall::contracts::AgentResultStatus::Completed},
      {"edge_balanced", dasall::contracts::AgentResultStatus::Completed},
      {"edge_minimal", dasall::contracts::AgentResultStatus::Completed},
      {"factory_test", dasall::contracts::AgentResultStatus::PartiallyCompleted},
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

    auto mock_llm_manager = std::dynamic_pointer_cast<MockLLMManager>(dependency_set->llm_manager);
    assert_true(mock_llm_manager != nullptr,
                "true integration fixture should expose MockLLMManager for request inspection: " +
                    profile_case.profile_id);
    mock_llm_manager->clear_recorded_requests();

    dasall::runtime::AgentFacade facade;
    auto init_request = make_true_integration_init_request(
        dependency_set,
        "rt-029-" + profile_case.profile_id,
        profile_case.profile_id,
        "cognition-profile-compatibility-" + profile_case.profile_id);
    init_request.policy_snapshot = load_profile_snapshot(profile_case.profile_id);
    const auto init_result = facade.init(init_request);
    assert_true(init_result.accepted,
                "profile compatibility case should initialize AgentFacade: " +
                    profile_case.profile_id);
    assert_true(init_result.resolved_profile_id == profile_case.profile_id,
                "runtime init should bind the resolved profile to the policy snapshot: " +
                    profile_case.profile_id);
    assert_true(init_result.diagnostics == "cognition_ports=composed_from_policy_snapshot",
                "runtime init should compose missing cognition ports from the runtime policy snapshot: " +
                    profile_case.profile_id);

    const auto result = facade.handle(make_agent_request(
        "req-029-" + profile_case.profile_id,
        "session-029-" + profile_case.profile_id,
        "trace-029-" + profile_case.profile_id,
        "query profile compatibility " + profile_case.profile_id));

    assert_true(result.status == profile_case.expected_status,
                "profile compatibility should return the expected terminal status for the active runtime profile: " +
                    profile_case.profile_id);
    assert_true(result.response_text.has_value() && !result.response_text->empty(),
                "profile compatibility should produce non-empty response_text: " +
                    profile_case.profile_id);
    assert_true(result.goal_id.has_value() && !result.goal_id->empty(),
                "profile compatibility should preserve goal id anchor: " +
                    profile_case.profile_id);

    const auto* planning_request =
        find_recorded_request(*mock_llm_manager, "planning", "plan");
    const auto* execution_request =
        find_recorded_request(*mock_llm_manager, "execution", "action_decision");
    const auto* reflection_request =
        find_recorded_request(*mock_llm_manager, "reflection", "failure_analysis");
    const auto* response_request =
        find_recorded_request(*mock_llm_manager, "response", "final_response");

    assert_true(planning_request != nullptr,
                "profile compatibility should drive the planning bridge request through the runtime-projected policy: " +
                    profile_case.profile_id);
    assert_true(execution_request != nullptr,
                "profile compatibility should drive the execution bridge request through the runtime-projected policy: " +
                    profile_case.profile_id);
    assert_true(reflection_request != nullptr,
                "profile compatibility should drive the reflection bridge request through the runtime-projected policy: " +
                    profile_case.profile_id);

    const auto& stage_routes = init_request.policy_snapshot->model_profile().stage_routes;
    const auto expected_timeout_ms =
        static_cast<std::uint32_t>(init_request.policy_snapshot->timeout_policy().llm.timeout_ms);
    const auto expected_output_tokens =
        init_request.policy_snapshot->token_budget_policy().max_output_tokens;

    assert_true(planning_request->request.model_route.has_value() &&
                    *planning_request->request.model_route == stage_routes.at("planning").route,
                "planning route should come from the runtime policy snapshot instead of the stage-name default: " +
                    profile_case.profile_id);
    assert_true(execution_request->request.model_route.has_value() &&
                    *execution_request->request.model_route == stage_routes.at("execution").route,
                "execution route should come from the runtime policy snapshot instead of the stage-name default: " +
                    profile_case.profile_id);
    assert_true(reflection_request->request.model_route.has_value() &&
                    *reflection_request->request.model_route == stage_routes.at("reflection").route,
                "reflection route should come from the runtime policy snapshot instead of the stage-name default: " +
                    profile_case.profile_id);
    assert_true(planning_request->request.timeout_ms.has_value() &&
                    *planning_request->request.timeout_ms == expected_timeout_ms,
                "planning deadline should come from the runtime policy snapshot: " +
                    profile_case.profile_id);
    assert_true(execution_request->request.timeout_ms.has_value() &&
                    *execution_request->request.timeout_ms == expected_timeout_ms,
                "execution deadline should come from the runtime policy snapshot: " +
                    profile_case.profile_id);
    assert_true(reflection_request->request.timeout_ms.has_value() &&
                    *reflection_request->request.timeout_ms == expected_timeout_ms,
                "reflection deadline should come from the runtime policy snapshot: " +
                    profile_case.profile_id);
    assert_true(planning_request->request.max_output_tokens.has_value() &&
                    *planning_request->request.max_output_tokens == expected_output_tokens,
                "planning output budget should come from the runtime policy snapshot: " +
                    profile_case.profile_id);

    if (profile_case.profile_id == "factory_test") {
      assert_true(response_request == nullptr,
                  "factory_test should prefer template fallback before issuing a response bridge request");
      assert_true(result.status == dasall::contracts::AgentResultStatus::PartiallyCompleted,
                  "factory_test should surface explicit template fallback as a degraded terminal status");
    } else {
      if (response_request != nullptr) {
        assert_true(response_request->request.model_route.has_value() &&
                        *response_request->request.model_route == stage_routes.at("response").route,
                    "response route should come from the runtime policy snapshot instead of the stage-name default: " +
                        profile_case.profile_id);
        assert_true(response_request->request.timeout_ms.has_value() &&
                        *response_request->request.timeout_ms == expected_timeout_ms,
                    "response deadline should come from the runtime policy snapshot: " +
                        profile_case.profile_id);
      }
    }

    if (dependency_set->memory_manager != nullptr) {
      dependency_set->memory_manager->shutdown();
    }
    cleanup_database_artifacts(database_path);
  }
}

void test_missing_canonical_route_is_rejected_at_runtime_init() {
  const auto database_path =
      make_temp_database_path("dasall-cognition-profile-missing-response-route");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-029-missing-route",
      "turn-029-missing-route-001",
      "query profile compatibility missing response route");

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-029-missing-route",
      "desktop_full",
      "cognition-profile-compatibility-missing-response-route",
      std::string{"response"}));
  assert_true(!init_result.accepted,
              "runtime init should fail closed when the policy snapshot is missing a canonical response route");
  assert_true(init_result.error_code ==
                  static_cast<std::int32_t>(dasall::contracts::ResultCode::PolicyDenied),
              "missing canonical route should map to PolicyDenied at init time");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_cognition_profile_compatibility_matrix();
    test_missing_canonical_route_is_rejected_at_runtime_init();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
