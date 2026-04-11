#pragma once

#include <cstdint>
#include <string>

namespace dasall::llm {

struct NormalizedUsageRecord {
  std::uint32_t prompt_tokens = 0;
  std::uint32_t completion_tokens = 0;
  std::uint32_t total_tokens = 0;
  std::uint32_t prompt_cache_hit_tokens = 0;
  std::uint32_t prompt_cache_miss_tokens = 0;
  double estimated_cost_usd = 0.0;
  std::string provider_id;
  std::string model_id;
  std::string pricing_ref;
};

}  // namespace dasall::llm
