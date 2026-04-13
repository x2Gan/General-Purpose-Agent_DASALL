#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/UsageAggregator.h"

#include "ModelRouterTestSupport.h"

namespace {

using dasall::llm::AdapterUsageFragment;
using dasall::llm::UsageAggregator;
using dasall::llm::provider::ProviderModelMetadata;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

const ProviderModelMetadata& require_model(std::string provider_id, std::string model_id) {
  static const auto catalog = dasall::llm::test_support::make_default_catalog();
  const auto* model = catalog.find_model(provider_id, model_id);
  if (model == nullptr) {
    throw std::runtime_error("observability fixture model metadata is missing");
  }

  return *model;
}

void test_usage_record_keeps_cost_anchor_fields_observable() {
  UsageAggregator aggregator;
  const auto usage = aggregator.aggregate(
      AdapterUsageFragment{
          .prompt_tokens = 180U,
          .completion_tokens = 20U,
          .total_tokens = 200U,
          .prompt_cache_hit_tokens = 60U,
          .prompt_cache_miss_tokens = 120U,
      },
      require_model("deepseek-prod", "deepseek-chat"));

  assert_equal(std::string("deepseek-prod"), usage.provider_id,
               "UsageAggregator should keep provider_id visible for downstream observability sinks");
  assert_equal(std::string("deepseek-chat"), usage.model_id,
               "UsageAggregator should keep model_id visible for downstream observability sinks");
  assert_equal(std::string("pricing-2026.04.13"), usage.pricing_ref,
               "UsageAggregator should keep pricing_ref visible for downstream observability sinks");
  assert_true(usage.estimated_cost_usd > 0.0,
              "UsageAggregator should keep estimated_cost_usd visible for downstream observability sinks");
}

void test_missing_pricing_metadata_gracefully_falls_back_to_zero_cost() {
  UsageAggregator aggregator;
  auto model = require_model("deepseek-prod", "deepseek-chat");
  model.pricing_ref.clear();
  model.summary.input_cache_hit_usd_per_1m = 0.0;
  model.summary.input_cache_miss_usd_per_1m = 0.0;
  model.summary.output_usd_per_1m = 0.0;

  const auto usage = aggregator.aggregate(
      AdapterUsageFragment{
          .prompt_tokens = 200U,
          .completion_tokens = 50U,
          .total_tokens = 250U,
          .prompt_cache_hit_tokens = std::nullopt,
          .prompt_cache_miss_tokens = std::nullopt,
      },
      model);

  assert_equal(200, static_cast<int>(usage.prompt_tokens),
               "UsageAggregator should keep prompt_tokens even when pricing metadata is absent");
  assert_equal(50, static_cast<int>(usage.completion_tokens),
               "UsageAggregator should keep completion_tokens even when pricing metadata is absent");
  assert_true(usage.estimated_cost_usd == 0.0,
              "UsageAggregator should gracefully fall back to zero estimated cost when pricing metadata is absent");
}

}  // namespace

int main() {
  try {
    test_usage_record_keeps_cost_anchor_fields_observable();
    test_missing_pricing_metadata_gracefully_falls_back_to_zero_cost();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}