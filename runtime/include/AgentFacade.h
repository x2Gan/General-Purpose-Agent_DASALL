#pragma once

#include <memory>

#include "IAgent.h"

namespace dasall::runtime {

class AgentFacade final : public IAgent {
 public:
  AgentFacade();
  ~AgentFacade() override;

  AgentFacade(const AgentFacade&) = delete;
  AgentFacade& operator=(const AgentFacade&) = delete;

  AgentInitResult init(const AgentInitRequest& request) override;
  contracts::AgentResult handle(const contracts::AgentRequest& request) override;
  contracts::AgentResult resume(const ResumeHandleRequest& request) override;
  bool stop(std::uint32_t timeout_ms) override;

 private:
  class State;
  std::unique_ptr<State> state_;
};

}  // namespace dasall::runtime