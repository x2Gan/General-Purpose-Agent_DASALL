#include <exception>
#include <iostream>
#include <string>

#include "AgentOrchestrator.h"
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
      .resume_token = "resume-token-021",
      .trace_context = "trace-resume-021",
      .override_options = std::nullopt,
  };
}

}  // namespace

int main() {
  using dasall::contracts::AgentResultStatus;
  using dasall::contracts::CheckpointState;
  using dasall::runtime::AgentOrchestrator;
  using dasall::runtime::OrchestratorComposition;
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

    OrchestratorComposition waiting_composition;
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
    assert_true(resumed_result.stage_trace.size() == 5,
                "continue_from_checkpoint path should still expose five stages");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}