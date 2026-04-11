#pragma once

#include <cstdint>
#include <string>

namespace dasall::llm::prompt {

struct PromptComposerConfig {
  std::string template_engine;
  std::uint32_t max_few_shot_count = 5;
};

}  // namespace dasall::llm::prompt