#include <cmath>
#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"

#include "ModelRouterTestSupport.h"

namespace {

using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::PromptEvalStatus;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::UsageAggregator;
using dasall::llm::execution::ResponseNormalizer;
using dasall::llm::execution::ResponseNormalizerContext;
using dasall::llm::provider::ProviderModelMetadata;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ResponseNormalizerContext make_context() {
  return ResponseNormalizerContext{
      .route_key = "deepseek-prod/deepseek-chat",
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-chat",
      .model_name = "deepseek-chat",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .prompt_eval_status = PromptEvalStatus::Stable,
      .prompt_release_scope = "desktop_full",
      .request_id = "req-normalizer-003",
      .llm_call_id = "call-normalizer-003",
      .completed_at_ms = 1710001003000,
  };
}

AdapterCallResult make_result() {
  LLMResponse response;
  response.request_id = "req-normalizer-003";
  response.llm_call_id = "call-normalizer-003";
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = "normalized-usage";
  response.completed_at = 1710001003000;
  response.finish_reason = "stop";

  AdapterCallResult result;
  result.response = std::move(response);
  return result;
}

const ProviderModelMetadata& require_model(std::string provider_id, std::string model_id) {
  static const auto catalog = dasall::llm::test_support::make_default_catalog();
  const auto* model = catalog.find_model(provider_id, model_id);
  if (model == nullptr) {
    throw std::runtime_error("usage test fixture model metadata is missing");
  }

  return *model;
}

void test_usage_fragment_promotes_token_counts_into_shared_response() {
  ResponseNormalizer normalizer;
  auto adapter_result = make_result();
  adapter_result.usage = AdapterUsageFragment{
      .prompt_tokens = 120U,
      .completion_tokens = 30U,
      .total_tokens = 150U,
      .prompt_cache_hit_tokens = std::nullopt,
      .prompt_cache_miss_tokens = std::nullopt,
  };

  const auto result = normalizer.normalize(adapter_result, make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should promote usage fragments into the shared response token fields");
  assert_true(result.usage_fragment.has_value(),
              "ResponseNormalizer should keep the normalized usage fragment for UsageAggregator consumers");
  assert_equal(120, static_cast<int>(*result.response->input_tokens),
               "ResponseNormalizer should copy prompt_tokens into LLMResponse.input_tokens");
  assert_equal(30, static_cast<int>(*result.response->output_tokens),
               "ResponseNormalizer should copy completion_tokens into LLMResponse.output_tokens");
  assert_equal(150, static_cast<int>(*result.response->total_tokens),
               "ResponseNormalizer should copy total_tokens into LLMResponse.total_tokens");
}

void test_cache_hit_and_miss_usage_is_preserved_for_usage_aggregator() {
  ResponseNormalizer normalizer;
  auto adapter_result = make_result();
  adapter_result.usage = AdapterUsageFragment{
      .prompt_tokens = 150U,
      .completion_tokens = 20U,
      .total_tokens = 170U,
      .prompt_cache_hit_tokens = 50U,
      .prompt_cache_miss_tokens = 100U,
  };

  const auto result = normalizer.normalize(adapter_result, make_context());

  assert_true(result.has_consistent_values() && result.succeeded(),
              "ResponseNormalizer should preserve provider-specific cache hit/miss usage for downstream cost aggregation");
  assert_true(result.usage_fragment.has_value(),
              "ResponseNormalizer should keep cache hit/miss usage in the module-local fragment");
  assert_equal(50, static_cast<int>(*result.usage_fragment->prompt_cache_hit_tokens),
               "ResponseNormalizer should preserve prompt_cache_hit_tokens for UsageAggregator");
  assert_equal(100, static_cast<int>(*result.usage_fragment->prompt_cache_miss_tokens),
               "ResponseNormalizer should preserve prompt_cache_miss_tokens for UsageAggregator");
}

void test_usage_aggregator_calculates_standard_cost_without_cache() {
  ResponseNormalizer normalizer;
  UsageAggregator aggregator;
  auto adapter_result = make_result();
  adapter_result.usage = AdapterUsageFragment{
      .prompt_tokens = 120U,
      .completion_tokens = 30U,
      .total_tokens = 150U,
      .prompt_cache_hit_tokens = std::nullopt,
      .prompt_cache_miss_tokens = std::nullopt,
  };

  const auto normalized = normalizer.normalize(adapter_result, make_context());
  const auto usage = aggregator.aggregate(*normalized.usage_fragment,
                                          require_model("deepseek-prod", "deepseek-chat"));

  assert_equal(120, static_cast<int>(usage.prompt_tokens),
               "UsageAggregator should preserve prompt token totals for standard usage");
  assert_equal(30, static_cast<int>(usage.completion_tokens),
               "UsageAggregator should preserve completion token totals for standard usage");
  assert_equal(120, static_cast<int>(usage.prompt_cache_miss_tokens),
               "UsageAggregator should treat prompt tokens as cache misses when no cache split is provided");
  assert_true(std::fabs(usage.estimated_cost_usd - 0.000042) < 1e-12,
              "UsageAggregator should price standard usage with the miss-input and output rates from provider metadata");
}

void test_usage_aggregator_calculates_split_cache_pricing() {
  ResponseNormalizer normalizer;
  UsageAggregator aggregator;
  auto adapter_result = make_result();
  adapter_result.usage = AdapterUsageFragment{
      .prompt_tokens = 150U,
      .completion_tokens = 20U,
      .total_tokens = 170U,
      .prompt_cache_hit_tokens = 50U,
      .prompt_cache_miss_tokens = 100U,
  };

  const auto normalized = normalizer.normalize(adapter_result, make_context());
  const auto usage = aggregator.aggregate(*normalized.usage_fragment,
                                          require_model("deepseek-prod", "deepseek-chat"));

  assert_equal(50, static_cast<int>(usage.prompt_cache_hit_tokens),
               "UsageAggregator should preserve prompt_cache_hit_tokens for split pricing");
  assert_equal(100, static_cast<int>(usage.prompt_cache_miss_tokens),
               "UsageAggregator should preserve prompt_cache_miss_tokens for split pricing");
  assert_true(std::fabs(usage.estimated_cost_usd - 0.0000305) < 1e-12,
              "UsageAggregator should apply cache-hit, cache-miss, and output rates independently");
}

}  // namespace

int main() {
  try {
    test_usage_fragment_promotes_token_counts_into_shared_response();
    test_cache_hit_and_miss_usage_is_preserved_for_usage_aggregator();
    test_usage_aggregator_calculates_standard_cost_without_cache();
    test_usage_aggregator_calculates_split_cache_pricing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}