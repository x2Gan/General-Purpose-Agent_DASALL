#pragma once

#include <optional>
#include <vector>

#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime::TransitionGuardTable {

struct TransitionGuardRule {
  std::vector<TransitionGuardFact> all_of;
  std::vector<TransitionGuardFact> any_of;

  [[nodiscard]] bool satisfied_by(const StateTransitionRequest& request) const;
  [[nodiscard]] std::optional<TransitionGuardFact> first_unsatisfied_guard(
      const StateTransitionRequest& request) const;
};

[[nodiscard]] bool is_legal(RuntimeState from_state, RuntimeState to_state);

[[nodiscard]] std::optional<TransitionGuardRule> get_guard(
    RuntimeState from_state,
    RuntimeState to_state);

[[nodiscard]] std::optional<StateTransitionCheckpointHint> get_checkpoint_strategy(
    RuntimeState from_state,
    RuntimeState to_state);

}  // namespace dasall::runtime::TransitionGuardTable