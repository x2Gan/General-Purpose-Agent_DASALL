#include <exception>
#include <iostream>
#include <string>

#include "AgentOrchestrator.h"
#include "agent/AgentRequest.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::AgentRequest make_request(std::string request_id) {
  dasall::contracts::AgentRequest request;
  request.request_id = std::move(request_id);
  request.session_id = std::string{"session-001"};
  request.trace_id = std::string{"trace-001"};
  request.user_input = std::string{"summarize runtime status"};
  request.request_channel = dasall::contracts::RequestChannel::Cli;
  request.created_at = 1710000000000;
  return request;
}

}  // namespace

int main() {
  using dasall::contracts::AgentResultStatus;
  using dasall::runtime::AgentOrchestrator;
  using dasall::runtime::OrchestratorComposition;
  using dasall::runtime::OrchestratorStage;
  using dasall::runtime::StubMainLoopExit;
  using dasall::runtime::StubRecoveryExit;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    AgentOrchestrator direct_response_orchestrator;
    const auto direct_result = direct_response_orchestrator.run_once(make_request("req-direct"));

    assert_equal(5,
                 static_cast<int>(direct_result.stage_trace.size()),
                 "skeleton run must always expose five stage trace entries");
    assert_true(direct_result.stage_trace[0].stage == OrchestratorStage::Preflight,
                "first stage must remain preflight");
    assert_true(direct_result.stage_trace[0].entered,
                "preflight must be entered on direct response path");
    assert_true(direct_result.stage_trace[0].state_after ==
                    dasall::runtime::RuntimeState::Planning,
                "preflight should finish in Planning");
    assert_true(direct_result.stage_trace[1].stage == OrchestratorStage::MainLoop,
                "second stage must remain main_loop");
    assert_true(direct_result.stage_trace[1].state_after ==
                    dasall::runtime::RuntimeState::Responding,
                "direct main loop path should materialize response");
    assert_true(!direct_result.stage_trace[2].entered,
                "tool_round must be skipped on direct response path");
    assert_true(!direct_result.stage_trace[3].entered,
                "recovery_round must be skipped on direct response path");
    assert_true(direct_result.stage_trace[4].stage == OrchestratorStage::Terminalize,
                "last stage must remain terminalize");
    assert_true(direct_result.stage_trace[4].state_after ==
                    dasall::runtime::RuntimeState::Completed,
                "terminalize must complete the skeleton run");
    assert_true(!direct_result.used_tool_round,
                "direct response path should not mark tool round usage");
    assert_true(!direct_result.used_recovery_round,
                "direct response path should not mark recovery round usage");
    assert_true(direct_result.agent_result.status == AgentResultStatus::Completed,
                "direct response path should return completed AgentResult");
    assert_true(direct_result.agent_result.task_completed == true,
                "completed AgentResult must set task_completed=true");
    assert_true(direct_result.agent_result.response_text.has_value() &&
                    direct_result.agent_result.response_text->find("skeleton completed") !=
                        std::string::npos,
                "direct response path should expose deterministic stub response text");

    OrchestratorComposition fail_safe_composition;
    fail_safe_composition.stub_ports.main_loop_exit = StubMainLoopExit::ToolRound;
    fail_safe_composition.stub_ports.recovery_exit = StubRecoveryExit::AbortSafe;
    fail_safe_composition.stub_ports.fail_safe_response_text =
        "runtime orchestrator skeleton fail-safe response";

    AgentOrchestrator fail_safe_orchestrator(std::move(fail_safe_composition));
    const auto fail_safe_result = fail_safe_orchestrator.run_once(make_request("req-fail-safe"));

    assert_equal(5,
                 static_cast<int>(fail_safe_result.stage_trace.size()),
                 "fail-safe path must also preserve five stage trace entries");
    assert_true(fail_safe_result.used_tool_round,
                "tool path must mark tool_round as used");
    assert_true(fail_safe_result.used_recovery_round,
                "tool path must mark recovery_round as used");
    assert_true(fail_safe_result.stage_trace[2].entered,
                "tool_round must be entered on tool path");
    assert_true(fail_safe_result.stage_trace[2].state_after ==
                    dasall::runtime::RuntimeState::Reflecting,
                "tool_round should end in Reflecting before recovery evaluation");
    assert_true(fail_safe_result.stage_trace[3].entered,
                "recovery_round must be entered after tool observation");
    assert_true(fail_safe_result.stage_trace[3].state_after ==
                    dasall::runtime::RuntimeState::FailedSafe,
                "abort-safe recovery should route to FailedSafe");
    assert_true(fail_safe_result.stage_trace[4].state_before ==
                    dasall::runtime::RuntimeState::FailedSafe,
                "terminalize should pick up from FailedSafe on abort-safe path");
    assert_true(fail_safe_result.stage_trace[4].state_after ==
                    dasall::runtime::RuntimeState::Completed,
                "terminalize should still converge to Completed result emission");
    assert_true(fail_safe_result.agent_result.status == AgentResultStatus::Failed,
                "abort-safe path should return failed AgentResult");
    assert_true(fail_safe_result.agent_result.task_completed == false,
                "failed AgentResult must set task_completed=false");
    assert_true(fail_safe_result.agent_result.error_info.has_value(),
                "abort-safe path should expose runtime error info");
    assert_true(fail_safe_result.agent_result.error_info->details.stage == "recovery_round",
                "abort-safe error should point at recovery_round");
    assert_true(fail_safe_result.agent_result.response_text.has_value() &&
                    fail_safe_result.agent_result.response_text->find("fail-safe") !=
                        std::string::npos,
                "abort-safe path should expose deterministic fail-safe response text");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}