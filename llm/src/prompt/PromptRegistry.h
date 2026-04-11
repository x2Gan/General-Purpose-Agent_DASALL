#pragma once

#include "PromptAssetRepository.h"
#include "prompt/IPromptRegistry.h"

namespace dasall::llm::prompt {

class PromptRegistry final : public IPromptRegistry {
 public:
  bool init(const PromptRegistryConfig& config) override;
  PromptRegistryResult select(const PromptQuery& query) const override;

 private:
  PromptRegistryConfig config_;
  PromptAssetRepository repository_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::prompt