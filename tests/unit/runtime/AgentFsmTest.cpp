#include <exception>
#include <iostream>
#include <string>

#include "fsm/AgentFsm.h"
#include "support/TestAssertions.h"

int main() {
  using dasall::contracts::CheckpointState;
  using dasall::runtime::AgentFsm;
  using dasall::runtime::RuntimeState;
  using dasall::runtime::TransitionGuardFact;
  using dasall::runtime::TransitionViolationType;
  using dasall::runtime::runtime_state_catalog;
  using dasall::runtime::runtime_state_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    constexpr auto kStates = runtime_state_catalog();
    assert_equal(17, static_cast<int>(kStates.size()), "runtime FSM must expose exactly 17 states");
    assert_equal("Idle", std::string(runtime_state_name(kStates.front())),
                 "first runtime state must remain Idle");
    assert_equal("SafeMode", std::string(runtime_state_name(kStates.back())),
                 "last runtime state must remain SafeMode");

    AgentFsm clarify_fsm(RuntimeState::Reasoning);
    assert_true(clarify_fsm.current_state() == RuntimeState::Reasoning,
          "AgentFsm should expose configured initial state");

    const dasall::runtime::StateTransitionRequest clarify_request{
        .from_state = RuntimeState::Reasoning,
        .requested_to = RuntimeState::WaitingClarify,
        .transition_reason = "clarification needed",
        .guard_facts = {TransitionGuardFact::ClarificationNeeded,
                        TransitionGuardFact::ProfileAllowsClarify},
    };

    assert_true(clarify_fsm.can_enter(clarify_request),
                "guard-complete reasoning->waiting clarify transition should be allowed");
    const auto accepted_outcome = clarify_fsm.transition(clarify_request);
    assert_true(accepted_outcome.accepted, "legal transition should produce accepted outcome");
    assert_true(!accepted_outcome.has_rejection(),
                "accepted transition should not carry rejection reason");
    assert_true(accepted_outcome.resolved_state == RuntimeState::WaitingClarify,
                "accepted transition should land in WaitingClarify");
    assert_true(accepted_outcome.checkpoint_hint.writes_checkpoint(),
                "waiting clarify transition should request checkpoint write");
    assert_true(accepted_outcome.checkpoint_hint.checkpoint_state == CheckpointState::Paused,
                "waiting clarify transition should map to Paused checkpoint state");
    assert_true(accepted_outcome.checkpoint_hint.pending_action_required,
                "waiting clarify transition should require pending action persistence");
    assert_true(clarify_fsm.current_state() == RuntimeState::WaitingClarify,
          "accepted transition should update AgentFsm current_state");

    AgentFsm mismatch_fsm(RuntimeState::Reasoning);
    const dasall::runtime::StateTransitionRequest mismatch_request{
      .from_state = RuntimeState::WaitingClarify,
      .requested_to = RuntimeState::Receiving,
      .transition_reason = "stale state snapshot",
      .guard_facts = {TransitionGuardFact::AgentRequestAvailable},
    };

    assert_true(!mismatch_fsm.can_enter(mismatch_request),
          "stale from_state must fail can_enter");
    const auto mismatch_outcome = mismatch_fsm.transition(mismatch_request);
    assert_true(!mismatch_outcome.accepted, "source mismatch must be rejected");
    assert_true(mismatch_outcome.has_rejection(),
          "source mismatch must return rejection reason");
    assert_true(mismatch_outcome.rejection_reason->violation_type ==
            TransitionViolationType::SourceStateMismatch,
          "source mismatch must map to SourceStateMismatch");
    assert_true(mismatch_outcome.resolved_state == RuntimeState::Reasoning,
          "source mismatch rejection should preserve current runtime state");

    AgentFsm illegal_fsm(RuntimeState::WaitingClarify);

    const dasall::runtime::StateTransitionRequest illegal_request{
        .from_state = RuntimeState::WaitingClarify,
        .requested_to = RuntimeState::Idle,
        .transition_reason = "attempt to skip back to idle",
        .guard_facts = {},
    };

      assert_true(!illegal_fsm.can_enter(illegal_request),
                "unsupported edge must be rejected by can_enter");
      const auto rejected_outcome = illegal_fsm.transition(illegal_request);
    assert_true(!rejected_outcome.accepted, "illegal transition must not be accepted");
    assert_true(rejected_outcome.has_rejection(),
                "illegal transition must surface rejection reason");
    assert_true(rejected_outcome.rejection_reason->violation_type ==
                    TransitionViolationType::IllegalTransition,
                "illegal edge should be classified as IllegalTransition");

            AgentFsm guard_fsm(RuntimeState::Reasoning);
            const dasall::runtime::StateTransitionRequest missing_guard_request{
              .from_state = RuntimeState::Reasoning,
              .requested_to = RuntimeState::WaitingConfirm,
              .transition_reason = "high risk action without approval fact",
              .guard_facts = {},
            };

            assert_true(!guard_fsm.can_enter(missing_guard_request),
                  "missing required guard fact must fail can_enter");
            const auto guard_rejection = guard_fsm.transition(missing_guard_request);
            assert_true(!guard_rejection.accepted, "missing guard fact must reject transition");
            assert_true(guard_rejection.has_rejection(),
                  "missing guard fact must surface rejection reason");
            assert_true(guard_rejection.rejection_reason->violation_type ==
                    TransitionViolationType::MissingGuardFact,
                  "missing guard fact must map to MissingGuardFact");
            assert_true(guard_rejection.rejection_reason->violated_guard ==
                    TransitionGuardFact::HighRiskConfirmationRequired,
                  "missing guard fact rejection must identify violated guard");

            AgentFsm reflect_fsm(RuntimeState::Reflecting);
            const dasall::runtime::StateTransitionRequest reflect_request{
              .from_state = RuntimeState::Reflecting,
              .requested_to = RuntimeState::Reasoning,
              .transition_reason = "retry after reflection",
              .guard_facts = {TransitionGuardFact::RecoveryRetryAllowed},
            };

            assert_true(reflect_fsm.can_enter(reflect_request),
                  "OR guard transition should accept any allowed recovery fact");
            const auto reflect_outcome = reflect_fsm.transition(reflect_request);
            assert_true(reflect_outcome.accepted,
                  "reflecting->reasoning should accept a valid any_of guard");
            assert_true(reflect_outcome.checkpoint_hint.checkpoint_state == CheckpointState::Running,
                  "reflecting->reasoning should write Running checkpoint state");

            AgentFsm completed_fsm(RuntimeState::Completed);
            const dasall::runtime::StateTransitionRequest complete_to_idle_request{
              .from_state = RuntimeState::Completed,
              .requested_to = RuntimeState::Idle,
              .transition_reason = "result returned to facade",
              .guard_facts = {TransitionGuardFact::ResultReturned},
            };

            assert_true(!completed_fsm.is_terminal(RuntimeState::Completed),
                  "Completed must remain non-terminal because Completed->Idle is legal");
            assert_true(completed_fsm.can_enter(complete_to_idle_request),
                  "Completed->Idle should remain legal with ResultReturned guard");
            const auto complete_to_idle_outcome = completed_fsm.transition(complete_to_idle_request);
            assert_true(complete_to_idle_outcome.accepted,
                  "Completed->Idle should be accepted by real AgentFsm");
            assert_true(complete_to_idle_outcome.checkpoint_hint.mutation ==
                    dasall::runtime::CheckpointMutationKind::ClearActiveReference,
                  "Completed->Idle should clear active checkpoint reference");

            AgentFsm safe_mode_fsm(RuntimeState::SafeMode);
            const dasall::runtime::StateTransitionRequest safe_mode_exit_request{
              .from_state = RuntimeState::SafeMode,
              .requested_to = RuntimeState::Idle,
              .transition_reason = "attempt to leave terminal state",
              .guard_facts = {},
            };

            assert_true(safe_mode_fsm.is_terminal(RuntimeState::SafeMode),
                  "SafeMode should be treated as terminal by real AgentFsm");
            assert_true(!safe_mode_fsm.can_enter(safe_mode_exit_request),
                  "terminal state exit must fail can_enter");
            const auto safe_mode_rejection = safe_mode_fsm.transition(safe_mode_exit_request);
            assert_true(!safe_mode_rejection.accepted, "terminal state exit must be rejected");
            assert_true(safe_mode_rejection.has_rejection(),
                  "terminal state exit must surface rejection reason");
            assert_true(safe_mode_rejection.rejection_reason->violation_type ==
                    TransitionViolationType::TerminalStateExit,
                  "terminal state exit must map to TerminalStateExit");

            assert_true(!safe_mode_fsm.is_terminal(RuntimeState::Reasoning),
                  "Reasoning must remain non-terminal in real AgentFsm");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}