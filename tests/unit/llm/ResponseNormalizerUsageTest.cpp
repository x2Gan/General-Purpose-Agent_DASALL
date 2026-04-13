#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/execution/ResponseNormalizer.h"

namespace {

using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::execution::ResponseNormalizer;
using dasall::llm::execution::ResponseNormalizerContext;
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

}  // namespace

int main() {
  try {
    test_usage_fragment_promotes_token_counts_into_shared_response();
    test_cache_hit_and_miss_usage_is_preserved_for_usage_aggregator();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}