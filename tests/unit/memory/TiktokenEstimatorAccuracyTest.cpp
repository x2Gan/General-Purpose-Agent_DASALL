#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"
#include "util/TokenEstimator.h"

namespace {

struct TokenExpectation {
  std::string_view text;
  int expected_tokens;
};

void test_tiktoken_estimator_matches_openai_cl100k_reference_examples() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto estimator = dasall::memory::util::create_token_estimator(
      dasall::memory::TokenEstimatorBackend::Tiktoken);
  assert_true(
      dynamic_cast<const dasall::memory::util::HeuristicTokenEstimator*>(estimator.get()) ==
          nullptr,
      "tiktoken accuracy test requires the vendored cl100k tokenizer instead of the heuristic fallback");

  const std::vector<TokenExpectation> expectations{
      TokenExpectation{.text = "antidisestablishmentarianism", .expected_tokens = 6},
      TokenExpectation{.text = "2 + 2 = 4", .expected_tokens = 7},
      TokenExpectation{.text = "お誕生日おめでとう", .expected_tokens = 9},
  };

  for (const auto& expectation : expectations) {
    assert_equal(
        expectation.expected_tokens,
        estimator->estimate_text_tokens(expectation.text),
        "tiktoken estimator should match the published cl100k_base token count for: " +
            std::string(expectation.text));
  }
}

}  // namespace

int main() {
  try {
    test_tiktoken_estimator_matches_openai_cl100k_reference_examples();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}