#pragma once

#include "LLMSubsystemConfig.h"

#include <vector>

namespace dasall::llm::prompt {

struct PromptRegistryConfig {
  dasall::llm::PromptAssetSourceConfig asset_sources;
  std::vector<std::string> trusted_sources;
};

}  // namespace dasall::llm::prompt
