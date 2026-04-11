#pragma once

#include <cstdint>

namespace dasall::llm::prompt {

struct ModelBudgetHint {
  std::uint32_t context_window = 0;
  std::uint32_t max_output_tokens = 0;
  std::uint32_t reserved_output_tokens = 0;
};

}  // namespace dasall::llm::prompt