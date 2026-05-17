#include <cmath>
#include <exception>
#include <iostream>
#include <numeric>
#include <string>

#include "support/TestAssertions.h"
#include "vector/SimpleLocalEmbeddingAdapter.h"

namespace {

void test_simple_local_embedding_adapter_is_deterministic_and_normalized() {
  using dasall::tests::support::assert_true;

  const dasall::memory::SimpleLocalEmbeddingAdapter adapter;
  const auto first = adapter.embed("remember sqlite vector wiring");
  const auto second = adapter.embed("remember sqlite vector wiring");
  const auto different = adapter.embed("different semantic memory payload");

  assert_true(static_cast<int>(first.size()) == adapter.dimension(),
              "simple local embedding adapter should emit vectors that match its declared dimension");
  assert_true(first == second,
              "simple local embedding adapter should be deterministic for identical input");
  assert_true(first != different,
              "simple local embedding adapter should produce different embeddings for different token sets");

  const float squared_norm = std::inner_product(first.begin(), first.end(), first.begin(), 0.0F);
  assert_true(std::fabs(squared_norm - 1.0F) < 0.001F,
              "simple local embedding adapter should L2-normalize its embeddings");
}

}  // namespace

int main() {
  try {
    test_simple_local_embedding_adapter_is_deterministic_and_normalized();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}