#include <exception>
#include <iostream>
#include <string>

#include "fsm/IAgentFsm.h"
#include "support/TestAssertions.h"

namespace {

class FakeAgentFsm final : public dasall::runtime::IAgentFsm {
 public:
  [[nodiscard]] dasall::runtime::RuntimeState current_state() const override {
    return state_;
  }

  [[nodiscard]] bool can_enter(const dasall::runtime::StateTransitionRequest& request) const override {
    return request.from_state == state_ &&
           request.requested_to == dasall::runtime::RuntimeState::WaitingClarify &&
           request.has_guard(dasall::runtime::TransitionGuardFact::ClarificationNeeded) &&
           request.has_guard(dasall::runtime::TransitionGuardFact::ProfileAllowsClarify);
  }

  [[nodiscard]] dasall::runtime::StateTransitionOutcome transition(
      const dasall::runtime::StateTransitionRequest& request) override {
    using dasall::contracts::CheckpointState;
    using dasall::runtime::CheckpointMutationKind;
    using dasall::runtime::RuntimeState;
    using dasall::runtime::StateTransitionCheckpointHint;
    using dasall::runtime::TransitionViolationType;

    if (request.from_state != state_) {
      return dasall::runtime::make_rejected_transition_outcome(
          request,
          TransitionViolationType::SourceStateMismatch,
          "request.from_state must match current_state");
    }

    if (!can_enter(request)) {
      return dasall::runtime::make_rejected_transition_outcome(
          request,
          TransitionViolationType::IllegalTransition,
          "transition is not present in the frozen 007 surface test");
    }

    state_ = RuntimeState::WaitingClarify;
    return dasall::runtime::make_transition_outcome(
        request,
        state_,
        StateTransitionCheckpointHint{
            .mutation = CheckpointMutationKind::Write,
            .checkpoint_state = CheckpointState::Paused,
            .pending_action_required = true,
        });
  }

  [[nodiscard]] bool is_terminal(const dasall::runtime::RuntimeState state) const override {
    return state == dasall::runtime::RuntimeState::Completed ||
           state == dasall::runtime::RuntimeState::SafeMode;
  }

 private:
  dasall::runtime::RuntimeState state_ = dasall::runtime::RuntimeState::Reasoning;
};

}  // namespace

int main() {
  using dasall::contracts::CheckpointState;
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

    FakeAgentFsm fsm;
    assert_true(fsm.current_state() == RuntimeState::Reasoning,
                "fake FSM should start in Reasoning for surface validation");

    const dasall::runtime::StateTransitionRequest clarify_request{
        .from_state = RuntimeState::Reasoning,
        .requested_to = RuntimeState::WaitingClarify,
        .transition_reason = "clarification needed",
        .guard_facts = {TransitionGuardFact::ClarificationNeeded,
                        TransitionGuardFact::ProfileAllowsClarify},
    };

    assert_true(fsm.can_enter(clarify_request),
                "guard-complete reasoning->waiting clarify transition should be allowed");
    const auto accepted_outcome = fsm.transition(clarify_request);
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

    const dasall::runtime::StateTransitionRequest illegal_request{
        .from_state = RuntimeState::WaitingClarify,
        .requested_to = RuntimeState::Idle,
        .transition_reason = "attempt to skip back to idle",
        .guard_facts = {},
    };

    assert_true(!fsm.can_enter(illegal_request),
                "unsupported edge must be rejected by can_enter");
    const auto rejected_outcome = fsm.transition(illegal_request);
    assert_true(!rejected_outcome.accepted, "illegal transition must not be accepted");
    assert_true(rejected_outcome.has_rejection(),
                "illegal transition must surface rejection reason");
    assert_true(rejected_outcome.rejection_reason->violation_type ==
                    TransitionViolationType::IllegalTransition,
                "illegal edge should be classified as IllegalTransition");

    assert_true(fsm.is_terminal(RuntimeState::Completed),
                "Completed should be treated as a terminal state by the surface test double");
    assert_true(!fsm.is_terminal(RuntimeState::Reasoning),
                "Reasoning must remain non-terminal in the surface test double");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}