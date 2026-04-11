#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/TokenEstimator.h"

namespace {

void test_token_estimate_can_drive_model_router_context_gate() {
  using dasall::llm::TokenEstimator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const std::vector<std::string> messages = {
      std::string(120U, 'a'),
      "执行诊断并解释当前模型为什么需要更大上下文窗口",
  };

  TokenEstimator estimator;
  const auto small_window_estimate = estimator.estimate(messages, 32U, 16U);
  const auto large_window_estimate = estimator.estimate(messages, 512U, 16U);

  assert_equal(small_window_estimate.estimated_input_tokens,
               large_window_estimate.estimated_input_tokens,
               "ModelRouter hard filtering should depend on context_window, not on changing the input estimate");
  assert_true(small_window_estimate.over_budget,
              "ModelRouter should be able to reject candidate routes whose context window is too small");
  assert_true(!large_window_estimate.over_budget,
              "ModelRouter should keep routes whose context window can accommodate the estimated input");
}

void test_token_estimate_preserves_router_budget_fields() {
  using dasall::llm::TokenEstimator;
  using dasall::tests::support::assert_equal;

  TokenEstimator estimator;
  const auto estimate = estimator.estimate("route-selection", 64U, 24U);

  assert_equal(64U, estimate.context_window,
               "ModelRouter policy checks should receive the candidate context window back from TokenEstimate");
  assert_equal(24U, estimate.reserved_output_tokens,
               "ModelRouter policy checks should preserve reserved output budget in TokenEstimate");
}

}  // namespace

int main() {
  try {
    test_token_estimate_can_drive_model_router_context_gate();
    test_token_estimate_preserves_router_budget_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}