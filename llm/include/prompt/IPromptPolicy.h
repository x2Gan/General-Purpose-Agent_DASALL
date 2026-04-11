#pragma once

#include "prompt/PromptComposeResult.h"
#include "prompt/PromptPolicyConfig.h"
#include "prompt/PromptPolicyDecision.h"
#include "prompt/PromptPolicyInput.h"

namespace dasall::llm::prompt {

class IPromptPolicy {
 public:
  virtual ~IPromptPolicy() = default;

  virtual bool init(const PromptPolicyConfig& config) = 0;
  virtual PromptPolicyDecision evaluate(
      const dasall::contracts::PromptComposeResult& compose_result,
      const PromptPolicyInput& input) const = 0;
};

}  // namespace dasall::llm::prompt