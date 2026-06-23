#pragma once

#include <optional>
#include <string_view>

#include "prompt/PromptAssetMetadata.h"
#include "prompt/PromptQuery.h"
#include "prompt/PromptRegistryConfig.h"
#include "prompt/PromptRegistryResult.h"

namespace dasall::llm::prompt {

class IPromptRegistry {
 public:
  virtual ~IPromptRegistry() = default;

  virtual bool init(const PromptRegistryConfig& config) = 0;
  virtual PromptRegistryResult select(const PromptQuery& query) const = 0;
  [[nodiscard]] virtual std::optional<PromptAssetMetadata> lookup_release_asset(
      std::string_view prompt_release_id) const = 0;
};

}  // namespace dasall::llm::prompt
