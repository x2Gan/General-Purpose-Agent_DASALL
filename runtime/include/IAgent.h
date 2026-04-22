#pragma once

#include <cstdint>

#include "AgentTypes.h"
#include "agent/AgentRequest.h"
#include "agent/AgentResult.h"

namespace dasall::runtime {

class IAgent {
 public:
  virtual ~IAgent() = default;

  virtual AgentInitResult init(const AgentInitRequest& request) = 0;
  virtual contracts::AgentResult handle(const contracts::AgentRequest& request) = 0;
  virtual contracts::AgentResult resume(const ResumeHandleRequest& request) = 0;
  virtual bool stop(std::uint32_t timeout_ms) = 0;
};

}  // namespace dasall::runtime