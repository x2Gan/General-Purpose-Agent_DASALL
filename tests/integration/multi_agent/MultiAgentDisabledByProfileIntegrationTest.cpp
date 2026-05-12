#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include "IMultiAgentCoordinator.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "agent/MultiAgentRequest.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class TempStateRoot {
 public:
  explicit TempStateRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempStateRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_source_runtime_policy_snapshot(const std::string& profile_id) {
  const dasall::profiles::ProfileCatalog catalog(
      std::filesystem::path(DASALL_SOURCE_ROOT) / "profiles");
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto load_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{.profile_id = profile_id});
  assert_true(load_result.ok() && load_result.snapshot != nullptr,
              "multi_agent disabled integration should load runtime policy snapshot");
  return load_result.snapshot;
}

void multi_agent_disabled_by_profile_snapshot_is_typed() {
  const auto snapshot = load_source_runtime_policy_snapshot("desktop_full");
  assert_true(!snapshot->multi_agent_enabled(),
              "desktop_full should keep multi_agent disabled in the runtime snapshot");
}

void multi_agent_disabled_by_profile_composition_injects_null_coordinator() {
  const auto snapshot = load_source_runtime_policy_snapshot("desktop_full");
  const TempStateRoot state_root("dasall-multi-agent-disabled");
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      snapshot,
      "multi-agent.disabled.integration",
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = std::filesystem::path(DASALL_SOURCE_ROOT),
          .state_root_override = state_root.path(),
      });
  assert_true(composition.ok(),
              "multi_agent disabled integration should compose runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set->multi_agent_coordinator != nullptr,
              "multi_agent disabled integration should inject a null coordinator instead of leaving the slot empty");
  assert_true(!composition.dependency_set->multi_agent_coordinator->enabled(),
              "multi_agent disabled integration should expose the null coordinator on disabled profiles");

  const auto report = composition.dependency_set->multi_agent_coordinator->coordinate(
      dasall::contracts::MultiAgentRequest{
          .parent_request_id = std::string("req-disabled"),
          .parent_task_id = std::string("task-disabled"),
          .goal_fragment = std::string("disabled goal"),
          .plan_fragment = std::string("disabled plan"),
          .collaboration_mode = dasall::contracts::CollaborationMode::Sequential,
      },
      dasall::multi_agent::MultiAgentExecutionContext{});

  assert_true(report.disabled,
              "null coordinator should return a disabled execution report");
  assert_true(report.multi_agent_result.has_value(),
              "null coordinator should still return a structured multi_agent_result");
  assert_equal(std::string("continue_single_agent"),
               report.multi_agent_result->recommended_next_action.value_or(std::string{}),
               "null coordinator should explicitly direct runtime back to the single-agent path");
}

}  // namespace

int main() {
  try {
    multi_agent_disabled_by_profile_snapshot_is_typed();
    multi_agent_disabled_by_profile_composition_injects_null_coordinator();
  } catch (const std::exception& ex) {
    std::cerr << "[MultiAgentDisabledByProfileIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}