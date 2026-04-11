#pragma once

#include <cstdint>

namespace dasall::llm {

struct TokenEstimate {
  std::uint32_t estimated_input_tokens = 0;
  std::uint32_t reserved_output_tokens = 0;
  std::uint32_t context_window = 0;
  bool over_budget = false;
  double safety_margin = 0.05;
};

}  // namespace dasall::llm
