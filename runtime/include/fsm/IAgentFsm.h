#pragma once

#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime {

class IAgentFsm {
 public:
  virtual ~IAgentFsm() = default;

  [[nodiscard]] virtual RuntimeState current_state() const = 0;
  [[nodiscard]] virtual bool can_enter(const StateTransitionRequest& request) const = 0;
  [[nodiscard]] virtual StateTransitionOutcome transition(const StateTransitionRequest& request) = 0;
  [[nodiscard]] virtual bool is_terminal(RuntimeState state) const = 0;
};

}  // namespace dasall::runtime