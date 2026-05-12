#pragma once

#include <memory>

#include "agent/MultiAgentRequest.h"
#include "MultiAgentTypes.h"

namespace dasall::multi_agent {

class IMultiAgentCoordinator {
 public:
  virtual ~IMultiAgentCoordinator() = default;

  [[nodiscard]] virtual bool enabled() const = 0;
  [[nodiscard]] virtual MultiAgentExecutionReport coordinate(
      const contracts::MultiAgentRequest& request,
      const MultiAgentExecutionContext& context) const = 0;
};

[[nodiscard]] std::shared_ptr<IMultiAgentCoordinator> create_multi_agent_coordinator(bool enabled);
[[nodiscard]] std::shared_ptr<IMultiAgentCoordinator> create_null_multi_agent_coordinator();

}  // namespace dasall::multi_agent