#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "TokenEstimate.h"

namespace dasall::llm {

struct TokenEstimatorConfig {
  double english_chars_per_token = 4.0;
  double cjk_chars_per_token = 1.5;
  double other_chars_per_token = 2.0;
  double safety_margin = 0.05;

  [[nodiscard]] bool has_consistent_values() const {
    return english_chars_per_token > 0.0 && cjk_chars_per_token > 0.0 &&
           other_chars_per_token > 0.0 && safety_margin >= 0.0;
  }
};

class TokenEstimator {
 public:
  explicit TokenEstimator(TokenEstimatorConfig config = {});

  [[nodiscard]] TokenEstimate estimate(std::string_view text,
                                       std::uint32_t context_window,
                                       std::uint32_t reserved_output_tokens) const;
  [[nodiscard]] TokenEstimate estimate(const std::vector<std::string>& messages,
                                       std::uint32_t context_window,
                                       std::uint32_t reserved_output_tokens) const;

 private:
  TokenEstimatorConfig config_;
};

}  // namespace dasall::llm