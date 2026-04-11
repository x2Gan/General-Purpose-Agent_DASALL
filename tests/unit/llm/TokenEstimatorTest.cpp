#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/TokenEstimator.h"

namespace {

void test_estimator_handles_empty_input() {
  using dasall::llm::TokenEstimator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TokenEstimator estimator;
  const auto estimate = estimator.estimate("", 128U, 16U);

  assert_equal(0U, estimate.estimated_input_tokens,
               "TokenEstimator should report zero input tokens for empty text");
  assert_equal(16U, estimate.reserved_output_tokens,
               "TokenEstimator should preserve the reserved output budget");
  assert_equal(128U, estimate.context_window,
               "TokenEstimator should preserve the target context window");
  assert_true(!estimate.over_budget,
              "TokenEstimator should not report over_budget for empty text under a positive window");
  assert_true(estimate.safety_margin == 0.05,
              "TokenEstimator should expose the default safety margin ratio");
}

void test_estimator_uses_language_sensitive_heuristics() {
  using dasall::llm::TokenEstimator;
  using dasall::tests::support::assert_true;

  TokenEstimator estimator;
  const auto english_estimate = estimator.estimate(std::string(40U, 'a'), 256U, 32U);
  const auto chinese_estimate = estimator.estimate("你好世界你好世界你好世界", 256U, 32U);

  assert_true(english_estimate.estimated_input_tokens >= 10U &&
                  english_estimate.estimated_input_tokens <= 12U,
              "TokenEstimator should keep ASCII text near the 4 chars per token heuristic");
  assert_true(chinese_estimate.estimated_input_tokens >= 8U &&
                  chinese_estimate.estimated_input_tokens <= 10U,
              "TokenEstimator should keep Chinese text near the 1.5 chars per token heuristic");
  assert_true(chinese_estimate.estimated_input_tokens > english_estimate.estimated_input_tokens - 3U,
              "TokenEstimator should account for non-English text producing a denser token ratio");
}

void test_estimator_applies_custom_safety_margin_to_mixed_messages() {
  using dasall::llm::TokenEstimator;
  using dasall::llm::TokenEstimatorConfig;
  using dasall::tests::support::assert_true;

  const std::vector<std::string> messages = {
      "system: plan carefully",
      "user: 诊断当前 token 预算并保留输出空间",
  };

  TokenEstimator default_estimator;
  TokenEstimator conservative_estimator(TokenEstimatorConfig{
      .english_chars_per_token = 4.0,
      .cjk_chars_per_token = 1.5,
      .other_chars_per_token = 2.0,
      .safety_margin = 0.20,
  });

  const auto default_estimate = default_estimator.estimate(messages, 256U, 32U);
  const auto conservative_estimate = conservative_estimator.estimate(messages, 256U, 32U);

  assert_true(conservative_estimate.estimated_input_tokens > default_estimate.estimated_input_tokens,
              "TokenEstimator should increase estimated_input_tokens when safety_margin grows");
  assert_true(conservative_estimate.safety_margin == 0.20,
              "TokenEstimator should surface the configured safety margin on the estimate result");
}

}  // namespace

int main() {
  try {
    test_estimator_handles_empty_input();
    test_estimator_uses_language_sensitive_heuristics();
    test_estimator_applies_custom_safety_margin_to_mixed_messages();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}