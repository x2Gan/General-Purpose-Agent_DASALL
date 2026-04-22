#include "AgentOrchestrator.h"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "fsm/AgentFsm.h"

namespace dasall::runtime {
namespace {

constexpr std::int32_t kRuntimeOrchestratorSkeletonCompletedCode = 5002;
constexpr std::int32_t kRuntimeOrchestratorSkeletonFailedSafeCode = 5003;
constexpr std::int32_t kRuntimeOrchestratorSkeletonPreflightRejectedCode = 5004;
constexpr std::int32_t kRuntimeOrchestratorSkeletonInternalErrorCode = 5005;

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::string make_result_id(const RuntimeState final_state) {
  return std::string{"rt-orchestrator-"} + runtime_state_name(final_state) + "-" +
         std::to_string(current_time_ms());
}

[[nodiscard]] contracts::ErrorInfo make_runtime_error(
    const std::int32_t code,
    std::string message,
    const std::string& stage,
    const RuntimeState final_state) {
  return contracts::ErrorInfo{
      .failure_type = contracts::ResultCodeCategory::Runtime,
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = code,
          .message = std::move(message),
          .stage = stage,
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "runtime.agent_orchestrator",
          .ref_id = std::string{runtime_state_name(final_state)},
      },
  };
}

[[nodiscard]] contracts::AgentResult make_result(
    const contracts::AgentRequest& request,
    const RuntimeState final_state,
    const contracts::AgentResultStatus status,
    const std::int32_t result_code,
    const std::string& response_text,
    const std::optional<contracts::ErrorInfo>& error_info = std::nullopt) {
  contracts::AgentResult result;
  result.result_id = make_result_id(final_state);
  result.status = status;
  result.result_code = result_code;
  result.response_text = response_text;
  result.task_completed = (status == contracts::AgentResultStatus::Completed);
  result.created_at = current_time_ms();
  result.request_id = request.request_id;
  result.trace_id = request.trace_id;
  result.error_info = error_info;
  return result;
}

struct TransitionStep {
  RuntimeState to_state = RuntimeState::Idle;
  std::string reason;
  std::vector<TransitionGuardFact> guards;
};

struct TransitionFailure {
  RuntimeState state_before = RuntimeState::Idle;
  std::string stage;
  std::string detail;
};

[[nodiscard]] std::optional<TransitionFailure> apply_step(
    IAgentFsm& fsm,
    const std::string& stage,
    const TransitionStep& step) {
  const RuntimeState before = fsm.current_state();
  const StateTransitionRequest request{
      .from_state = before,
      .requested_to = step.to_state,
      .transition_reason = step.reason,
      .guard_facts = step.guards,
  };

  const auto outcome = fsm.transition(request);
  if (outcome.accepted) {
    return std::nullopt;
  }

  std::string detail = "transition rejected";
  if (outcome.rejection_reason.has_value()) {
    detail = outcome.rejection_reason->detail;
  }

  return TransitionFailure{
      .state_before = before,
      .stage = stage,
      .detail = std::move(detail),
  };
}

[[nodiscard]] std::optional<TransitionFailure> apply_steps(
    IAgentFsm& fsm,
    const std::string& stage,
    const std::vector<TransitionStep>& steps) {
  for (const auto& step : steps) {
    if (const auto failure = apply_step(fsm, stage, step); failure.has_value()) {
      return failure;
    }
  }

  return std::nullopt;
}

void push_trace(std::vector<OrchestratorStageTrace>* trace,
                const OrchestratorStage stage,
                const RuntimeState before,
                const RuntimeState after,
                const bool entered,
                std::string detail) {
  trace->push_back(OrchestratorStageTrace{
      .stage = stage,
      .state_before = before,
      .state_after = after,
      .entered = entered,
      .detail = std::move(detail),
  });
}

}  // namespace

const char* orchestrator_stage_name(const OrchestratorStage stage) {
  switch (stage) {
    case OrchestratorStage::Preflight:
      return "preflight";
    case OrchestratorStage::MainLoop:
      return "main_loop";
    case OrchestratorStage::ToolRound:
      return "tool_round";
    case OrchestratorStage::RecoveryRound:
      return "recovery_round";
    case OrchestratorStage::Terminalize:
      return "terminalize";
  }

  return "unknown";
}

AgentOrchestrator::AgentOrchestrator(OrchestratorComposition composition)
    : composition_(std::move(composition)) {}

std::unique_ptr<IAgentFsm> AgentOrchestrator::build_fsm() const {
  if (composition_.fsm_factory) {
    return composition_.fsm_factory();
  }

  return std::make_unique<AgentFsm>(RuntimeState::Idle);
}

OrchestratorRunResult AgentOrchestrator::run_once(const contracts::AgentRequest& request) {
  OrchestratorRunResult run_result;

  auto fsm = build_fsm();
  if (!fsm) {
    run_result.agent_result = make_result(
        request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator skeleton cannot build FSM",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           "fsm factory returned null",
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed));
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const RuntimeState initial_state = fsm->current_state();
  if (composition_.stub_ports.reject_preflight) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               initial_state,
               initial_state,
               true,
               "stub preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    run_result.agent_result = make_result(
        request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonPreflightRejectedCode,
        "runtime orchestrator skeleton rejected request during preflight",
        make_runtime_error(kRuntimeOrchestratorSkeletonPreflightRejectedCode,
                           "stub preflight rejected request",
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed));
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const RuntimeState preflight_before = fsm->current_state();
  if (const auto failure = apply_steps(
          *fsm,
          orchestrator_stage_name(OrchestratorStage::Preflight),
          {{.to_state = RuntimeState::Receiving,
            .reason = "accept unary request",
            .guards = {TransitionGuardFact::AgentRequestAvailable,
                       TransitionGuardFact::FacadeInitialized}},
           {.to_state = RuntimeState::Planning,
            .reason = "preflight finished",
            .guards = {TransitionGuardFact::RequestValidated,
                       TransitionGuardFact::SessionLoaded,
                       TransitionGuardFact::BudgetInitialized}}});
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               preflight_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator skeleton hit an illegal preflight transition",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed));
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }
  push_trace(&run_result.stage_trace,
             OrchestratorStage::Preflight,
             preflight_before,
             fsm->current_state(),
             true,
             "stub preflight admitted request and loaded runtime-local snapshot");

  const RuntimeState main_loop_before = fsm->current_state();
  std::vector<TransitionStep> main_loop_steps = {
      TransitionStep{.to_state = RuntimeState::Reasoning,
                     .reason = "context prepared for reasoning",
                     .guards = {TransitionGuardFact::ContextAssembled}},
  };
  if (composition_.stub_ports.main_loop_exit == StubMainLoopExit::ToolRound) {
    main_loop_steps.push_back(TransitionStep{
        .to_state = RuntimeState::ToolCalling,
        .reason = "stub main loop selected tool round",
        .guards = {TransitionGuardFact::ToolCallPlanned,
                   TransitionGuardFact::BudgetAllowsToolCall},
    });
  } else {
    main_loop_steps.push_back(TransitionStep{
        .to_state = RuntimeState::Responding,
        .reason = "stub main loop materialized direct response",
        .guards = {TransitionGuardFact::DirectResponseReady},
    });
  }

  if (const auto failure = apply_steps(
          *fsm, orchestrator_stage_name(OrchestratorStage::MainLoop), main_loop_steps);
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator skeleton hit an illegal main loop transition",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed));
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }
  run_result.used_tool_round =
      (composition_.stub_ports.main_loop_exit == StubMainLoopExit::ToolRound);
  push_trace(&run_result.stage_trace,
             OrchestratorStage::MainLoop,
             main_loop_before,
             fsm->current_state(),
             true,
             run_result.used_tool_round
                 ? "stub main loop routed to tool round"
                 : "stub main loop produced direct response");

  if (run_result.used_tool_round) {
    const RuntimeState tool_round_before = fsm->current_state();
    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            {{.to_state = RuntimeState::WaitingExternal,
              .reason = "stub tool dispatch submitted",
              .guards = {TransitionGuardFact::ToolDispatchSubmitted}},
             {.to_state = RuntimeState::Reflecting,
              .reason = "stub tool observation returned",
              .guards = {TransitionGuardFact::ExternalResultAvailable}}});
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator skeleton hit an illegal tool round transition",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed));
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               tool_round_before,
               fsm->current_state(),
               true,
               "stub tool round produced observation for reflection");

    const RuntimeState recovery_round_before = fsm->current_state();
    run_result.used_recovery_round = true;
    std::vector<TransitionStep> recovery_steps;
    if (composition_.stub_ports.recovery_exit == StubRecoveryExit::AbortSafe) {
      recovery_steps.push_back(TransitionStep{
          .to_state = RuntimeState::FailedSafe,
          .reason = "stub recovery selected fail-safe",
          .guards = {TransitionGuardFact::RecoveryAbortSafe},
      });
    } else {
      recovery_steps.push_back(TransitionStep{
          .to_state = RuntimeState::Reasoning,
          .reason = "stub recovery allowed continue",
          .guards = {TransitionGuardFact::ReflectionContinue},
      });
      recovery_steps.push_back(TransitionStep{
          .to_state = RuntimeState::Responding,
          .reason = "continued response after reflection",
          .guards = {TransitionGuardFact::DirectResponseReady},
      });
    }

    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::RecoveryRound),
            recovery_steps);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::RecoveryRound,
                 recovery_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator skeleton hit an illegal recovery transition",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed));
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               recovery_round_before,
               fsm->current_state(),
               true,
               composition_.stub_ports.recovery_exit == StubRecoveryExit::AbortSafe
                   ? "stub recovery round entered fail-safe"
                   : "stub recovery round returned to response path");
  } else {
    const RuntimeState current_state = fsm->current_state();
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               current_state,
               current_state,
               false,
               "skipped because direct response path did not require tools");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               current_state,
               current_state,
               false,
               "skipped because no tool observation required reflection");
  }

  const RuntimeState terminalize_before = fsm->current_state();
  std::vector<TransitionStep> terminalize_steps;
  if (terminalize_before == RuntimeState::FailedSafe) {
    terminalize_steps.push_back(TransitionStep{
        .to_state = RuntimeState::Responding,
        .reason = "terminalize fail-safe response",
        .guards = {},
    });
  }
  terminalize_steps.push_back(TransitionStep{
      .to_state = RuntimeState::Auditing,
      .reason = "response materialized",
      .guards = {TransitionGuardFact::ResponseMaterialized},
  });
  terminalize_steps.push_back(TransitionStep{
      .to_state = RuntimeState::Persisting,
      .reason = "audit committed",
      .guards = {TransitionGuardFact::AuditCommitted},
  });
  terminalize_steps.push_back(TransitionStep{
      .to_state = RuntimeState::Completed,
      .reason = "persistence confirmed",
      .guards = {TransitionGuardFact::PersistenceConfirmed},
  });

  if (const auto failure = apply_steps(
          *fsm,
          orchestrator_stage_name(OrchestratorStage::Terminalize),
          terminalize_steps);
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               terminalize_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator skeleton hit an illegal terminal transition",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed));
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }
  push_trace(&run_result.stage_trace,
             OrchestratorStage::Terminalize,
             terminalize_before,
             fsm->current_state(),
             true,
             terminalize_before == RuntimeState::FailedSafe
                 ? "fail-safe path materialized final response"
                 : "response audited and persisted");

  run_result.final_state = fsm->current_state();
  if (composition_.stub_ports.recovery_exit == StubRecoveryExit::AbortSafe &&
      run_result.used_recovery_round) {
    run_result.agent_result = make_result(
        request,
        run_result.final_state,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonFailedSafeCode,
        composition_.stub_ports.fail_safe_response_text,
        make_runtime_error(kRuntimeOrchestratorSkeletonFailedSafeCode,
                           "stub recovery path entered fail-safe",
                           orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                           RuntimeState::FailedSafe));
  } else {
    run_result.agent_result = make_result(
        request,
        run_result.final_state,
        contracts::AgentResultStatus::Completed,
        kRuntimeOrchestratorSkeletonCompletedCode,
        composition_.stub_ports.success_response_text);
  }

  return run_result;
}

}  // namespace dasall::runtime