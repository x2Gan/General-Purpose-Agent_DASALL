#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "IMultiAgentCoordinator.h"
#include "MultiAgentRuntimeFold.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "agent/MultiAgentRequest.h"
#include "observation/ObservationSource.h"
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
              "multi_agent pipeline integration should load runtime policy snapshot");
  return load_result.snapshot;
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
make_enabled_snapshot(const dasall::profiles::RuntimePolicySnapshot& base) {
  return std::make_shared<const dasall::profiles::RuntimePolicySnapshot>(
      base.generation(),
      base.effective_profile_id(),
      base.runtime_budget(),
      base.model_profile(),
      base.token_budget_policy(),
      base.prompt_policy(),
      base.capability_cache_policy(),
      base.degrade_policy(),
      base.timeout_policy(),
      base.execution_policy(),
      base.ops_policy(),
      base.worker_threads(),
      true);
}

void multi_agent_pipeline_generates_observation_and_compensation_hints() {
  const auto base_snapshot = load_source_runtime_policy_snapshot("desktop_full");
  const auto enabled_snapshot = make_enabled_snapshot(*base_snapshot);
  const TempStateRoot state_root("dasall-multi-agent-enabled");
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      enabled_snapshot,
      "multi-agent.pipeline.integration",
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = std::filesystem::path(DASALL_SOURCE_ROOT),
          .state_root_override = state_root.path(),
      });
  assert_true(composition.ok(),
              "multi_agent pipeline integration should compose runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set->multi_agent_coordinator != nullptr,
              "multi_agent pipeline integration should inject a real coordinator when the snapshot enables it");
  assert_true(composition.dependency_set->multi_agent_coordinator->enabled(),
              "multi_agent pipeline integration should expose the real coordinator on enabled snapshots");

  const auto report = composition.dependency_set->multi_agent_coordinator->coordinate(
      dasall::contracts::MultiAgentRequest{
          .parent_request_id = std::string("req-enabled"),
          .parent_task_id = std::string("task-enabled"),
          .goal_fragment = std::string("summarize evidence"),
          .plan_fragment = std::string("plan enabled pipeline"),
          .collaboration_mode = dasall::contracts::CollaborationMode::Concurrent,
          .worker_budget_guard = std::nullopt,
          .permission_guard = std::nullopt,
          .stop_conditions = std::nullopt,
      },
      dasall::multi_agent::MultiAgentExecutionContext{
          .runtime_instance_id = std::string("runtime-enabled"),
          .profile_id = enabled_snapshot->effective_profile_id(),
          .trace_id = std::string("trace-enabled"),
          .request_id = std::string("req-enabled"),
          .goal_id = std::string("goal-enabled"),
          .policy_snapshot_ref = std::string("policy-enabled"),
          .parent_checkpoint_ref = std::nullopt,
          .checkpoint = std::nullopt,
          .runtime_budget_snapshot = std::nullopt,
          .retry_count = std::nullopt,
          .latest_observation = std::nullopt,
          .tool_results = std::vector<dasall::contracts::ToolResult>{
              dasall::contracts::ToolResult{
                  .request_id = std::string("req-enabled"),
                  .tool_call_id = std::string("call-enabled"),
                  .tool_name = std::string("agent.dataset"),
                  .success = true,
                  .payload = std::string("{\"dataset\":\"agent.dataset\"}"),
                  .error = std::nullopt,
                  .side_effects = std::vector<std::string>{"tmp://multi-agent-enabled-artifact"},
                  .completed_at = 1710000180000,
                  .duration_ms = 8,
                  .goal_id = std::string("goal-enabled"),
                  .worker_task_id = std::string("worker-enabled"),
                  .tags = std::vector<std::string>{"integration", "multi_agent"},
              },
          },
          .allowed_tool_domains = std::vector<std::string>{"builtin"},
      });

  assert_true(!report.disabled,
              "real coordinator should return an enabled execution report");
  assert_true(report.graph_snapshot.has_value(),
              "real coordinator should project a subtask graph snapshot");
  assert_true(report.multi_agent_result.has_value(),
              "real coordinator should return a structured multi_agent_result");
  assert_equal(std::string("continue"),
               report.multi_agent_result->recommended_next_action.value_or(std::string{}),
               "real coordinator should keep the positive path on continue");
  assert_true(report.emitted_observations.size() == 1U,
              "real coordinator should emit one folded observation on the loopback path");
  assert_true(!report.compensation_hints.empty(),
              "real coordinator should route tool side effects through the compensation ledger");
  assert_true(!report.recovery_request.has_value(),
              "real coordinator should not emit a recovery request on the positive path");

  const auto folded = dasall::multi_agent::fold_multi_agent_report_for_runtime(report);
  assert_true(folded.latest_observation.has_value(),
              "runtime fold should surface the latest multi_agent observation");
  assert_true(folded.latest_observation->source == dasall::contracts::ObservationSource::WorkerAgent,
              "runtime fold should keep the observation source on WorkerAgent");
  assert_true(folded.latest_observation->success.value_or(false),
              "runtime fold should preserve the positive observation state");
  assert_true(!folded.compensation_hints.empty(),
              "runtime fold should preserve compensation hints for downstream audit and recovery consumers");
}

}  // namespace

int main() {
  try {
    multi_agent_pipeline_generates_observation_and_compensation_hints();
  } catch (const std::exception& ex) {
    std::cerr << "[MultiAgentCoordinatorPipelineTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}