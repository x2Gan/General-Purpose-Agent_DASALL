#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "fsm/IAgentFsm.h"

namespace dasall::runtime {

class AgentFsm final : public IAgentFsm {
 public:
  explicit AgentFsm(RuntimeState initial_state = RuntimeState::Idle);

  [[nodiscard]] RuntimeState current_state() const override;
  [[nodiscard]] bool can_enter(const StateTransitionRequest& request) const override;
  [[nodiscard]] StateTransitionOutcome transition(const StateTransitionRequest& request) override;
  [[nodiscard]] bool is_terminal(RuntimeState state) const override;

 private:
  struct EvaluationResult {
    bool accepted = false;
    std::optional<StateTransitionCheckpointHint> checkpoint_hint;
    TransitionViolationType violation_type = TransitionViolationType::IllegalTransition;
    std::optional<TransitionGuardFact> violated_guard;
    std::string detail;
  };

  [[nodiscard]] EvaluationResult evaluate_request_locked(
      const StateTransitionRequest& request) const;

  mutable std::mutex state_mutex_;
  RuntimeState current_state_;
};

}  // namespace dasall::runtime