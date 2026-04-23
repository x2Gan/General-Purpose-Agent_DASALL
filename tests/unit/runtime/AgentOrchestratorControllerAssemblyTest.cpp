#include <filesystem>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "AgentOrchestrator.h"
#include "IMemoryManager.h"
#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimePolicyProvider.h"
#include "checkpoint/CheckpointBuildTypes.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::AgentRequest make_request(std::string request_id) {
  dasall::contracts::AgentRequest request;
  request.request_id = std::move(request_id);
  request.session_id = std::string{"session-021"};
  request.trace_id = std::string{"trace-021"};
  request.user_input = std::string{"assemble runtime-local orchestrator"};
  request.request_channel = dasall::contracts::RequestChannel::Cli;
  request.created_at = 1710000000000;
  return request;
}

dasall::runtime::ResumeHandleRequest make_resume_request(
    const std::string& session_id,
    const std::string& checkpoint_ref) {
  return dasall::runtime::ResumeHandleRequest{
      .request_id = "resume-021",
      .session_id = session_id,
      .checkpoint_ref = checkpoint_ref,
      .resume_reason = "user clarification received",
            .resume_token = dasall::runtime::make_resume_binding_token(session_id, checkpoint_ref),
      .trace_context = "trace-resume-021",
      .override_options = std::nullopt,
  };
}

[[nodiscard]] std::filesystem::path repository_root() {
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> load_snapshot(
        const std::string& profile_id) {
    const dasall::profiles::ProfileCatalog catalog(repository_root() / "profiles");
    const dasall::profiles::RuntimePolicyProvider provider(catalog);
    const auto runtime_result = provider.load_snapshot(dasall::profiles::RuntimePolicyLoadRequest{
            .profile_id = profile_id,
    });
    if (!runtime_result.ok() || runtime_result.snapshot == nullptr) {
        throw std::runtime_error("failed to load runtime policy snapshot for " + profile_id);
    }

    return runtime_result.snapshot;
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_snapshot_with_budget_degrade(
        const std::string& profile_id,
        const bool allow_budget_degrade) {
    const auto snapshot = load_snapshot(profile_id);
    auto degrade_policy = snapshot->degrade_policy();
    degrade_policy.allow_budget_degrade = allow_budget_degrade;

    return std::make_shared<dasall::profiles::RuntimePolicySnapshot>(
            snapshot->generation(),
            snapshot->effective_profile_id() +
                    (allow_budget_degrade ? std::string("-budget-degrade")
                                                                : std::string("-abort-safe")),
            snapshot->runtime_budget(),
            snapshot->model_profile(),
            snapshot->token_budget_policy(),
            snapshot->prompt_policy(),
            snapshot->capability_cache_policy(),
            std::move(degrade_policy),
            snapshot->timeout_policy(),
            snapshot->execution_policy(),
            snapshot->ops_policy(),
            snapshot->worker_threads());
}

[[nodiscard]] dasall::contracts::BudgetSnapshot make_incomplete_budget_snapshot() {
  using dasall::contracts::BudgetSnapshot;
  using dasall::contracts::BudgetSnapshotEntry;
  using dasall::contracts::BudgetType;

  return BudgetSnapshot{
      .snapshot_at_ms = 1710000000001,
      .entries = {
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Token,
              .current = 128,
              .max = 2048,
              .remaining = 1920,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Turn,
              .current = 1,
              .max = 6,
              .remaining = 5,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::ToolCall,
              .current = 0,
              .max = 2,
              .remaining = 2,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Latency,
              .current = 120,
              .max = 2000,
              .remaining = 1880,
              .reject_reason = std::nullopt,
          },
      },
      .overall_reject_reason = std::nullopt,
  };
}

[[nodiscard]] dasall::contracts::BudgetSnapshot make_invalid_budget_snapshot() {
  using dasall::contracts::BudgetSnapshot;
  using dasall::contracts::BudgetSnapshotEntry;
  using dasall::contracts::BudgetType;

  return BudgetSnapshot{
      .snapshot_at_ms = 1710000000002,
      .entries = {
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Token,
              .current = 128,
              .max = 2048,
              .remaining = 1920,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Turn,
              .current = 1,
              .max = 6,
              .remaining = 5,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::ToolCall,
              .current = 1,
              .max = 2,
              .remaining = 1,
              .reject_reason = std::string("tool_call_budget_exhausted"),
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Latency,
              .current = 120,
              .max = 2000,
              .remaining = 1880,
              .reject_reason = std::nullopt,
          },
          BudgetSnapshotEntry{
              .budget_type = BudgetType::Replan,
              .current = 0,
              .max = 2,
              .remaining = 2,
              .reject_reason = std::nullopt,
          },
      },
      .overall_reject_reason = std::string("tool_call_budget_exhausted"),
  };
}

class CountingMemoryManager final : public dasall::memory::IMemoryManager {
 public:
    dasall::contracts::ResultCode init(const dasall::memory::MemoryConfig&) override {
        return static_cast<dasall::contracts::ResultCode>(0);
    }

    void shutdown() noexcept override {}

    [[nodiscard]] dasall::memory::ContextAssemblyResult prepare_context(
            const dasall::memory::MemoryContextRequest& request) override {
        ++prepare_context_calls;
        last_request = request;

        dasall::memory::ContextAssemblyResult result;
        result.context_packet.request_id = request.request_id;
        result.context_packet.user_turn = request.goal_summary;
        result.context_packet.current_goal_summary = request.goal_summary;
        result.context_packet.recent_history = std::vector<std::string>{};
        result.context_packet.latest_observation_digest_summary =
                request.latest_observation_digest_summary;
        result.context_packet.active_tools = request.visible_tools;
        return result;
    }

    [[nodiscard]] dasall::memory::WritebackResult write_back(
            const dasall::memory::MemoryWritebackRequest&) override {
        return {};
    }

    [[nodiscard]] dasall::memory::WorkingMemoryExportResult export_working_memory_snapshot(
            const dasall::memory::WorkingMemoryExportRequest&) override {
        return {};
    }

    [[nodiscard]] dasall::memory::MaintenanceReport run_maintenance(
            const dasall::memory::MaintenanceRequest&) override {
        return {};
    }

    int prepare_context_calls = 0;
    std::optional<dasall::memory::MemoryContextRequest> last_request;
};

}  // namespace

int main() {
  using dasall::contracts::AgentResultStatus;
  using dasall::contracts::CheckpointState;
  using dasall::runtime::AgentOrchestrator;
  using dasall::runtime::OrchestratorComposition;
    using dasall::runtime::RuntimeErrorCode;
    using dasall::runtime::RuntimeState;
  using dasall::runtime::StubMainLoopExit;
  using dasall::runtime::StubRecoveryExit;
  using dasall::runtime::find_checkpoint_tag_value;
  using dasall::runtime::kRuntimeCheckpointBudgetSchemaVersion;
  using dasall::runtime::kRuntimeCheckpointBudgetSchemaVersionTag;
  using dasall::runtime::kRuntimeCheckpointFsmStateEnumVersion;
  using dasall::runtime::kRuntimeCheckpointFsmStateEnumVersionTag;
  using dasall::runtime::kRuntimeCheckpointSchemaVersion;
  using dasall::runtime::kRuntimeCheckpointSchemaVersionTag;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    AgentOrchestrator direct_orchestrator;
    const auto direct_result = direct_orchestrator.run_once(make_request("req-021-direct"));

    assert_true(direct_result.agent_result.status == AgentResultStatus::Completed,
                "direct path should assemble a completed AgentResult");
    assert_true(direct_result.checkpoint.has_value(),
                "direct path should materialize a completion checkpoint");
    assert_true(direct_result.effective_session.has_value(),
                "direct path should persist effective session state");
    assert_true(direct_result.checkpoint->state == CheckpointState::Succeeded,
                "direct path should save succeeded checkpoint state");
    assert_equal(
        std::string(kRuntimeCheckpointSchemaVersion),
        find_checkpoint_tag_value(*direct_result.checkpoint, kRuntimeCheckpointSchemaVersionTag)
            .value_or(std::string()),
        "direct path checkpoint must carry runtime schema version tag");
    assert_equal(
        std::string(kRuntimeCheckpointFsmStateEnumVersion),
        find_checkpoint_tag_value(
            *direct_result.checkpoint,
            kRuntimeCheckpointFsmStateEnumVersionTag)
            .value_or(std::string()),
        "direct path checkpoint must carry fsm schema version tag");
    assert_equal(
        std::string(kRuntimeCheckpointBudgetSchemaVersion),
        find_checkpoint_tag_value(
            *direct_result.checkpoint,
            kRuntimeCheckpointBudgetSchemaVersionTag)
            .value_or(std::string()),
        "direct path checkpoint must carry budget schema version tag");
    assert_true(direct_result.agent_result.checkpoint_ref ==
                    direct_result.checkpoint->checkpoint_id,
                "direct path AgentResult should reference final checkpoint");
    assert_true(!direct_result.used_tool_round && !direct_result.used_recovery_round,
                "direct path should not consume tool or recovery rounds");

    OrchestratorComposition fail_safe_composition;
    fail_safe_composition.stub_ports.main_loop_exit = StubMainLoopExit::ToolRound;
    fail_safe_composition.stub_ports.recovery_exit = StubRecoveryExit::AbortSafe;
    fail_safe_composition.stub_ports.fail_safe_response_text =
        "runtime-local fail-safe response";
    AgentOrchestrator fail_safe_orchestrator(std::move(fail_safe_composition));
    const auto fail_safe_result = fail_safe_orchestrator.run_once(make_request("req-021-fail-safe"));

    assert_true(fail_safe_result.used_tool_round,
                "abort-safe path should use scheduler-backed tool round");
    assert_true(fail_safe_result.used_recovery_round,
                "abort-safe path should use recovery manager");
    assert_true(fail_safe_result.scheduler_backpressure.has_value(),
                "abort-safe path should expose scheduler backpressure snapshot");
    assert_true(fail_safe_result.recovery_outcome.has_value(),
                "abort-safe path should expose recovery outcome");
    assert_true(fail_safe_result.recovery_outcome->executed_action == std::string("abort_safe"),
                "recovery manager should execute abort_safe action");
    assert_true(fail_safe_result.checkpoint.has_value() &&
                    fail_safe_result.checkpoint->state == CheckpointState::Failed,
                "abort-safe path should persist failed checkpoint anchor");
    assert_true(fail_safe_result.agent_result.status == AgentResultStatus::Failed,
                "abort-safe path should return failed AgentResult");
    assert_true(fail_safe_result.agent_result.error_info.has_value(),
                "abort-safe path should surface runtime error info");

    OrchestratorComposition degraded_composition;
    degraded_composition.policy_snapshot = load_snapshot("desktop_full");
    degraded_composition.stub_ports.main_loop_exit = StubMainLoopExit::ToolRound;
    degraded_composition.stub_ports.recovery_exit = StubRecoveryExit::BudgetExhausted;
    degraded_composition.default_runtime_budget = dasall::contracts::RuntimeBudget{
        .max_tokens = 1000,
        .max_turns = 4,
        .max_tool_calls = 1,
        .max_latency_ms = 1500,
        .max_replan_count = 2,
    };
    AgentOrchestrator degraded_orchestrator(std::move(degraded_composition));
    const auto degraded_result = degraded_orchestrator.run_once(make_request("req-021-degraded"));

    assert_true(degraded_result.recovery_outcome.has_value(),
                "budget-exhausted recovery path should expose recovery outcome");
    assert_true(degraded_result.recovery_outcome->executed_action == std::string("degrade"),
                "budget exhaustion should still produce a degrade recovery fact");
    assert_true(degraded_result.effective_session.has_value() &&
                    degraded_result.effective_session->fsm_state == RuntimeState::Degraded,
                "desktop_full profile should let orchestrator persist degraded terminal state");
    assert_true(degraded_result.agent_result.error_info.has_value() &&
                    degraded_result.agent_result.error_info->details.code ==
                        static_cast<std::int32_t>(RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED),
                "desktop_full budget exhaustion should surface degraded-mode error code");

    OrchestratorComposition failed_safe_budget_composition;
    failed_safe_budget_composition.policy_snapshot =
        load_snapshot_with_budget_degrade("desktop_full", false);
    failed_safe_budget_composition.stub_ports.main_loop_exit = StubMainLoopExit::ToolRound;
    failed_safe_budget_composition.stub_ports.recovery_exit = StubRecoveryExit::BudgetExhausted;
    failed_safe_budget_composition.default_runtime_budget = dasall::contracts::RuntimeBudget{
        .max_tokens = 1000,
        .max_turns = 4,
        .max_tool_calls = 1,
        .max_latency_ms = 1500,
        .max_replan_count = 2,
    };
    AgentOrchestrator failed_safe_budget_orchestrator(
        std::move(failed_safe_budget_composition));
    const auto failed_safe_budget_result =
        failed_safe_budget_orchestrator.run_once(make_request("req-021-budget-failsafe"));

    assert_true(failed_safe_budget_result.recovery_outcome.has_value(),
                "edge_minimal budget exhaustion should still expose recovery outcome");
    assert_true(
        failed_safe_budget_result.recovery_outcome->executed_action == std::string("degrade"),
        "policy override should not rewrite the recovery fact emitted by RecoveryManager");
    assert_true(failed_safe_budget_result.effective_session.has_value() &&
                    failed_safe_budget_result.effective_session->fsm_state ==
                        RuntimeState::FailedSafe,
                "edge_minimal profile should collapse the same recovery fact into failed-safe");
    assert_true(failed_safe_budget_result.agent_result.error_info.has_value() &&
                    failed_safe_budget_result.agent_result.error_info->details.code ==
                        static_cast<std::int32_t>(RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED),
                "edge_minimal budget exhaustion should surface failed-safe escalation code");

    auto waiting_memory_manager = std::make_shared<CountingMemoryManager>();
    auto waiting_dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
    waiting_dependency_set->memory_manager = waiting_memory_manager;

    OrchestratorComposition waiting_composition;
    waiting_composition.dependency_set = waiting_dependency_set;
    waiting_composition.stub_ports.main_loop_exit = StubMainLoopExit::WaitingClarify;
    waiting_composition.stub_ports.waiting_response_text =
        "runtime waiting for user clarification";
    AgentOrchestrator waiting_orchestrator(std::move(waiting_composition));
    const auto waiting_result = waiting_orchestrator.run_once(make_request("req-021-wait"));

    assert_true(waiting_result.checkpoint.has_value(),
                "waiting path should materialize resumable checkpoint");
    assert_true(waiting_result.checkpoint->state == CheckpointState::Paused,
                "waiting clarify path should persist paused checkpoint state");
    assert_true(waiting_result.resume_plan.has_value(),
                "waiting path should expose resume plan for controller assembly test");
    assert_true(waiting_result.effective_session.has_value() &&
                    waiting_result.effective_session->pending_interaction.has_value() &&
                    waiting_result.effective_session->pending_interaction->active(),
                "waiting path should bind pending interaction into session snapshot");
    assert_true(waiting_result.agent_result.status == AgentResultStatus::PartiallyCompleted,
                "waiting path should return resumable partial result");

    const auto resumed_result = waiting_orchestrator.handle_waiting_state(
        *waiting_result.effective_session,
        make_resume_request(
            waiting_result.effective_session->session_id,
            waiting_result.checkpoint->checkpoint_id.value_or(std::string())));

    assert_true(resumed_result.resume_plan.has_value(),
                "handle_waiting_state should rebuild resume plan before continuing");
    assert_true(resumed_result.agent_result.status == AgentResultStatus::Completed,
                "resume path should converge back to completed AgentResult");
    assert_true(resumed_result.checkpoint.has_value() &&
                    resumed_result.checkpoint->state == CheckpointState::Succeeded,
                "resume path should save succeeded checkpoint after completion");
    assert_true(resumed_result.agent_result.checkpoint_ref ==
                    resumed_result.checkpoint->checkpoint_id,
                "resume result should reference the new completion checkpoint");
    assert_equal(1,
                 waiting_memory_manager->prepare_context_calls,
                 "resume path should refresh context through memory manager exactly once");
    assert_true(waiting_memory_manager->last_request.has_value() &&
                    waiting_memory_manager->last_request->session_id ==
                        waiting_result.effective_session->session_id,
                "resume path should refresh context for the active waiting session");
    assert_true(waiting_memory_manager->last_request.has_value() &&
                    waiting_memory_manager->last_request->goal_summary ==
                        std::string("user clarification received"),
                "resume path should pass resume_reason into the refreshed context request");
    assert_true(resumed_result.stage_trace.size() == 5,
                "continue_from_checkpoint path should still expose five stages");

    AgentOrchestrator malformed_resume_orchestrator;
    malformed_resume_orchestrator.seed_for_test(*waiting_result.effective_session, {});
    malformed_resume_orchestrator.seed_checkpoint_for_test(
        *waiting_result.checkpoint,
        make_incomplete_budget_snapshot());

    const auto malformed_resume_result = malformed_resume_orchestrator.continue_from_checkpoint(
        *waiting_result.resume_plan,
        *waiting_result.effective_session);

    assert_true(malformed_resume_result.agent_result.status == AgentResultStatus::Failed,
                "resume path should fail closed when checkpoint budget sidecar is malformed");
    assert_true(
        malformed_resume_result.agent_result.response_text.has_value() &&
            malformed_resume_result.agent_result.response_text->find(
                "failed to initialize resume budget") != std::string::npos,
        "malformed budget sidecar should fail while restoring resume budget");
    assert_true(
        malformed_resume_result.agent_result.error_info.has_value() &&
            malformed_resume_result.agent_result.error_info->details.message.find(
                "budget snapshot is missing one or more dimensions") != std::string::npos,
        "malformed budget sidecar should preserve the missing-dimensions detail");

    AgentOrchestrator invalid_resume_orchestrator;
    invalid_resume_orchestrator.seed_for_test(*waiting_result.effective_session, {});
    invalid_resume_orchestrator.seed_checkpoint_for_test(
        *waiting_result.checkpoint,
        make_invalid_budget_snapshot());

    const auto invalid_resume_result = invalid_resume_orchestrator.continue_from_checkpoint(
        *waiting_result.resume_plan,
        *waiting_result.effective_session);

    assert_true(
        invalid_resume_result.agent_result.status == AgentResultStatus::Failed,
        "resume path should fail closed when checkpoint budget sidecar has inconsistent remaining/reject_reason semantics");
    assert_true(
        invalid_resume_result.agent_result.response_text.has_value() &&
            invalid_resume_result.agent_result.response_text->find(
                "failed to initialize resume budget") != std::string::npos,
        "invalid budget sidecar should still fail in resume budget restoration");
    assert_true(
        invalid_resume_result.agent_result.error_info.has_value() &&
            invalid_resume_result.agent_result.error_info->details.message.find(
                "reject_reason must be empty when not over budget") != std::string::npos,
        "invalid budget sidecar should preserve the reject_reason consistency detail");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}