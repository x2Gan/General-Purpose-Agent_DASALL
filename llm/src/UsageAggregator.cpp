#include "UsageAggregator.h"

#include <algorithm>
#include <cstdint>

namespace {

std::uint32_t derive_cache_hit_tokens(const dasall::llm::AdapterUsageFragment& usage_fragment) {
  if (usage_fragment.prompt_cache_hit_tokens.has_value()) {
    return *usage_fragment.prompt_cache_hit_tokens;
  }

  if (usage_fragment.prompt_tokens.has_value() && usage_fragment.prompt_cache_miss_tokens.has_value() &&
      *usage_fragment.prompt_cache_miss_tokens <= *usage_fragment.prompt_tokens) {
    return *usage_fragment.prompt_tokens - *usage_fragment.prompt_cache_miss_tokens;
  }

  return 0U;
}

std::uint32_t derive_cache_miss_tokens(const dasall::llm::AdapterUsageFragment& usage_fragment) {
  if (usage_fragment.prompt_cache_miss_tokens.has_value()) {
    return *usage_fragment.prompt_cache_miss_tokens;
  }

  if (usage_fragment.prompt_tokens.has_value()) {
    const auto cache_hit_tokens = derive_cache_hit_tokens(usage_fragment);
    return cache_hit_tokens <= *usage_fragment.prompt_tokens
               ? *usage_fragment.prompt_tokens - cache_hit_tokens
               : 0U;
  }

  return 0U;
}

double token_cost_usd(std::uint32_t tokens, double rate_per_1m_tokens) {
  if (tokens == 0U || rate_per_1m_tokens <= 0.0) {
    return 0.0;
  }

  return (static_cast<double>(tokens) * rate_per_1m_tokens) / 1'000'000.0;
}

bool pricing_metadata_available(const dasall::llm::provider::ProviderModelMetadata& model_metadata) {
  return !model_metadata.pricing_ref.empty() &&
         (model_metadata.summary.input_cache_hit_usd_per_1m > 0.0 ||
          model_metadata.summary.input_cache_miss_usd_per_1m > 0.0 ||
          model_metadata.summary.output_usd_per_1m > 0.0);
}

}  // namespace

namespace dasall::llm {

NormalizedUsageRecord UsageAggregator::aggregate(
    const AdapterUsageFragment& usage_fragment,
    const provider::ProviderModelMetadata& model_metadata) const {
  NormalizedUsageRecord record;
  record.provider_id = model_metadata.summary.provider_id;
  record.model_id = model_metadata.summary.model_id;
  record.pricing_ref = model_metadata.pricing_ref;

  if (!usage_fragment.has_consistent_values() || !usage_fragment.has_token_counts()) {
    return record;
  }

  record.prompt_tokens = *usage_fragment.prompt_tokens;
  record.completion_tokens = *usage_fragment.completion_tokens;
  record.total_tokens = *usage_fragment.total_tokens;
  record.prompt_cache_hit_tokens = std::min(record.prompt_tokens,
                                            derive_cache_hit_tokens(usage_fragment));
  record.prompt_cache_miss_tokens = std::min(record.prompt_tokens,
                                             derive_cache_miss_tokens(usage_fragment));

  if (!pricing_metadata_available(model_metadata)) {
    record.estimated_cost_usd = 0.0;
    return record;
  }

  record.estimated_cost_usd =
      token_cost_usd(record.prompt_cache_hit_tokens,
                     model_metadata.summary.input_cache_hit_usd_per_1m) +
      token_cost_usd(record.prompt_cache_miss_tokens,
                     model_metadata.summary.input_cache_miss_usd_per_1m) +
      token_cost_usd(record.completion_tokens,
                     model_metadata.summary.output_usd_per_1m);
  return record;
}

}  // namespace dasall::llm