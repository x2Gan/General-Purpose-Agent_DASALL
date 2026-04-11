#pragma once

#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptPipelineConfig.h"
#include "prompt/PromptPipelineResult.h"
#include "prompt/PromptPolicyInput.h"
#include "prompt/PromptQuery.h"

namespace dasall::llm::prompt {

class IPromptPipeline {
 public:
  virtual ~IPromptPipeline() = default;

  virtual bool init(const PromptPipelineConfig& config) = 0;
  virtual PromptPipelineResult run(
      const PromptQuery& query,
      const dasall::contracts::PromptComposeRequest& compose_request,
      const PromptPolicyInput& policy_input) const = 0;
};

}  // namespace dasall::llm::prompt
