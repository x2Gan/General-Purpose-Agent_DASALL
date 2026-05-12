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
#include "checkpoint/Checkpoint.h"
#include "error/ErrorInfo.h"
#include "observation/Observation.h"
#include "recovery/RecoveryManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CheckpointState;
using dasall::contracts::ObservationSource;
using dasall::runtime::RecoveryManager;
using dasall::runtime::RuntimeErrorCode;
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
              "multi_agent recovery integration should load runtime policy snapshot");
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

[[nodiscard]] dasall::contracts::BudgetSnapshot make_budget_snapshot() {
  return dasall::contracts::BudgetSnapshot{
      .snapshot_at_ms = 1710000181000,
      .entries = {
          dasall::contracts::BudgetSnapshotEntry{
              .budget_type = dasall::contracts::BudgetType::Token,
              .current = 256,
              .max = 4096,
              .remaining = 3840,
              .reject_reason = std::nullopt,
          },
          dasall::contracts::BudgetSnapshotEntry{
              .budget_type = dasall::contracts::BudgetType::Turn,
              .current = 2,
              .max = 6,
              .remaining = 4,
              .reject_reason = std::nullopt,
          },
          dasall::contracts::BudgetSnapshotEntry{
              .budget_type = dasall::contracts::BudgetType::ToolCall,
              .current = 1,
              .max = 3,
              .remaining = 2,
              .reject_reason = std::nullopt,
          },
          dasall::contracts::BudgetSnapshotEntry{
              .budget_type = dasall::contracts::BudgetType::Latency,
              .current = 700,
              .max = 8000,
              .remaining = 7300,
              .reject_reason = std::nullopt,
          },
          dasall::contracts::BudgetSnapshotEntry{
              .budget_type = dasall::contracts::BudgetType::Replan,
              .current = 0,
              .max = 2,
              .remaining = 2,
              .reject_reason = std::nullopt,
          },
      },
      .overall_reject_reason = std::nullopt,
  };
}

void multi_agent_recovery_fold_routes_abort_safe_through_recovery_manager() {
  const auto base_snapshot = load_source_runtime_policy_snapshot("desktop_full");
  const auto enabled_snapshot = make_enabled_snapshot(*base_snapshot);
  const TempStateRoot state_root("dasall-multi-agent-recovery");
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      enabled_snapshot,
      "multi-agent.recovery.integration",
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = std::filesystem::path(DASALL_SOURCE_ROOT),
          .state_root_override = state_root.path(),
      });
  assert_true(composition.ok(),
              "multi_agent recovery integration should compose runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set->multi_agent_coordinator != nullptr &&
                  composition.dependency_set->multi_agent_coordinator->enabled(),
              "multi_agent recovery integration should compose the real coordinator");

  const auto report = composition.dependency_set->multi_agent_coordinator->coordinate(
      dasall::contracts::MultiAgentRequest{
          .parent_request_id = std::string("req-recovery"),
          .parent_task_id = std::string("task-recovery"),
          .goal_fragment = std::string("recover collaboration"),
          .plan_fragment = std::string("plan recovery fold"),
          .collaboration_mode = dasall::contracts::CollaborationMode::Handoff,
          .worker_budget_guard = std::nullopt,
          .permission_guard = std::nullopt,
          .stop_conditions = std::nullopt,
      },
      dasall::multi_agent::MultiAgentExecutionContext{
          .runtime_instance_id = std::string("runtime-recovery"),
          .profile_id = enabled_snapshot->effective_profile_id(),
          .trace_id = std::string("trace-recovery"),
          .request_id = std::string("req-recovery"),
          .goal_id = std::string("goal-recovery"),
          .policy_snapshot_ref = std::string("policy-recovery"),
          .parent_checkpoint_ref = std::nullopt,
          .checkpoint = dasall::contracts::Checkpoint{
              .checkpoint_id = std::string("chk-recovery"),
              .state = CheckpointState::Running,
              .step_id = std::string("multi-agent-step"),
              .working_memory_snapshot = std::string("wm:multi-agent:recovery"),
              .pending_action = std::string{},
              .request_id = std::string("req-recovery"),
              .goal_id = std::string("goal-recovery"),
              .belief_state_ref = std::string("belief-recovery"),
              .retry_count = 1,
              .created_at = 1710000181001,
              .tags = std::vector<std::string>{"integration", "multi_agent"},
          },
          .runtime_budget_snapshot = make_budget_snapshot(),
          .retry_count = 1,
          .latest_observation = dasall::contracts::Observation{
              .observation_id = std::string("obs-recovery-input"),
              .source = ObservationSource::ToolExecution,
              .success = false,
              .payload = std::string("{}"),
              .created_at = 1710000181002,
              .error = dasall::contracts::ErrorInfo{
                  .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
                  .retryable = true,
                  .safe_to_replan = false,
                  .details = dasall::contracts::ErrorDetails{
                      .code = 5002,
                      .message = std::string("upstream tool failure needs recovery"),
                      .stage = std::string("tool_round"),
                  },
                  .source_ref = dasall::contracts::ErrorSourceRefMinimal{
                      .ref_type = std::string("tool_call"),
                      .ref_id = std::string("call-recovery-input"),
                  },
              },
              .side_effects = std::nullopt,
              .tool_call_id = std::string("call-recovery-input"),
              .worker_task_id = std::nullopt,
              .request_id = std::string("req-recovery"),
              .goal_id = std::string("goal-recovery"),
              .duration_ms = 12,
              .tags = std::vector<std::string>{"integration", "multi_agent"},
          },
          .tool_results = std::vector<dasall::contracts::ToolResult>{
              dasall::contracts::ToolResult{
                  .request_id = std::string("req-recovery"),
                  .tool_call_id = std::string("call-recovery-side-effect"),
                  .tool_name = std::string("agent.dataset"),
                  .success = true,
                  .payload = std::string("{\"ok\":true}"),
                  .error = std::nullopt,
                  .side_effects = std::vector<std::string>{"tmp://multi-agent-recovery-artifact"},
                  .completed_at = 1710000181003,
                  .duration_ms = 10,
                  .goal_id = std::string("goal-recovery"),
                  .worker_task_id = std::string("worker-recovery"),
                  .tags = std::vector<std::string>{"integration", "multi_agent"},
              },
          },
          .allowed_tool_domains = std::vector<std::string>{"builtin"},
      });

  assert_true(report.multi_agent_result.has_value(),
              "multi_agent recovery integration should return a structured result");
  assert_equal(std::string("abort_safe"),
               report.multi_agent_result->recommended_next_action.value_or(std::string{}),
               "multi_agent recovery integration should switch to abort_safe when compensation hints are present on a failed path");
  assert_true(!report.compensation_hints.empty(),
              "multi_agent recovery integration should produce compensation hints on the failed path");
  assert_true(report.recovery_request.has_value(),
              "multi_agent recovery integration should package a recovery request sidecar");

  const auto folded = dasall::multi_agent::fold_multi_agent_report_for_runtime(report);
  assert_true(folded.latest_observation.has_value(),
              "multi_agent recovery integration should fold the latest observation back to runtime");
  assert_true(folded.recovery_request.has_value(),
              "multi_agent recovery integration should keep the recovery request after folding");

  RecoveryManager recovery_manager;
  const auto plan = recovery_manager.evaluate(*folded.recovery_request);
  assert_true(plan.escalated() && plan.safe_failure_hint.has_value() &&
                  plan.safe_failure_hint->enter_safe_mode,
              "RecoveryManager should keep abort_safe as the runtime-owned escalation decision");

  const auto outcome = recovery_manager.execute(plan);
  assert_equal(std::string("abort_safe"),
               outcome.executed_action.value_or(std::string{}),
               "RecoveryManager should execute abort_safe on the multi_agent recovery path");
  assert_equal(std::string("FailedSafe"),
               outcome.final_runtime_state.value_or(std::string{}),
               "abort_safe should end in FailedSafe");

  const auto apply_result = recovery_manager.apply(outcome);
  assert_true(apply_result.applied &&
                  apply_result.error_code == RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
              "abort_safe apply should retain the runtime-owned safe-mode error code");
}

}  // namespace

int main() {
  try {
    multi_agent_recovery_fold_routes_abort_safe_through_recovery_manager();
  } catch (const std::exception& ex) {
    std::cerr << "[MultiAgentRecoveryFoldIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}