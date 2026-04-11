#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "prompt/ModelBudgetHint.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/TokenEstimator.h"

namespace {

void test_token_estimate_marks_prompt_composer_budget_overflow() {
  using dasall::llm::TokenEstimator;
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const ModelBudgetHint budget_hint{
      .context_window = 24U,
      .max_output_tokens = 8U,
      .reserved_output_tokens = 8U,
  };

  TokenEstimator estimator;
  const auto estimate = estimator.estimate(
      std::vector<std::string>{std::string(80U, 'a'), std::string(80U, 'b')},
      budget_hint.context_window, budget_hint.reserved_output_tokens);

  assert_equal(budget_hint.reserved_output_tokens, estimate.reserved_output_tokens,
               "PromptComposer budget checks should consume reserved_output_tokens from TokenEstimate");
  assert_true(estimate.over_budget,
              "PromptComposer should receive over_budget=true when estimated input exceeds the model budget");
}

void test_token_estimate_stays_within_prompt_composer_budget_for_short_prompt() {
  using dasall::llm::TokenEstimator;
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::tests::support::assert_true;

  const ModelBudgetHint budget_hint{
      .context_window = 256U,
      .max_output_tokens = 64U,
      .reserved_output_tokens = 32U,
  };

  TokenEstimator estimator;
  const auto estimate = estimator.estimate(
      std::vector<std::string>{"system: concise plan", "user: summarize diagnostics"},
      budget_hint.context_window, budget_hint.reserved_output_tokens);

  assert_true(!estimate.over_budget,
              "PromptComposer should see over_budget=false for short prompts under a generous window");
}

}  // namespace

int main() {
  try {
    test_token_estimate_marks_prompt_composer_budget_overflow();
    test_token_estimate_stays_within_prompt_composer_budget_for_short_prompt();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}