#pragma once

#include <string>
#include <vector>

namespace dasall::llm::prompt {

struct PromptRegistryConfig {
  std::string asset_root;
  std::vector<std::string> trusted_sources;
};

}  // namespace dasall::llm::prompt
