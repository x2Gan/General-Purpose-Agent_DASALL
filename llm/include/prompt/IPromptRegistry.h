#pragma once

#include "prompt/PromptQuery.h"
#include "prompt/PromptRegistryConfig.h"
#include "prompt/PromptRegistryResult.h"

namespace dasall::llm::prompt {

class IPromptRegistry {
 public:
  virtual ~IPromptRegistry() = default;

  virtual bool init(const PromptRegistryConfig& config) = 0;
  virtual PromptRegistryResult select(const PromptQuery& query) const = 0;
};

}  // namespace dasall::llm::prompt
