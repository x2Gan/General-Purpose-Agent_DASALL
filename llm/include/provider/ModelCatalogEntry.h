#pragma once

#include <cstdint>
#include <string>

namespace dasall::llm {

struct ModelCatalogEntry {
  std::string provider_id;
  std::string model_id;
  std::string model_version;
  std::string tier_family;
  std::string latency_tier;
  std::string cost_tier;
  std::string reasoning_depth_tier;
  std::uint32_t context_window = 0;
  std::uint32_t default_max_output_tokens = 0;
  std::uint32_t max_output_tokens_hard_limit = 0;
  bool supports_tools = false;
  bool supports_reasoning = false;
  bool supports_visible_reasoning = false;
  bool supports_prompt_cache = false;
  double input_cache_hit_usd_per_1m = 0.0;
  double input_cache_miss_usd_per_1m = 0.0;
  double output_usd_per_1m = 0.0;
  std::string metadata_source_uri;
  std::string metadata_effective_at;
  std::string verification_state;
};

}  // namespace dasall::llm
