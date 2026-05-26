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

void test_simple_local_embedding_adapter_handles_utf8_text() {
  using dasall::tests::support::assert_true;

  const dasall::memory::SimpleLocalEmbeddingAdapter adapter;
  const auto first = adapter.embed("上下文编排器 提示词组合器");
  const auto second = adapter.embed("上下文编排器 提示词组合器");
  const auto different = adapter.embed("恢复准入 全局主控");

  assert_true(static_cast<int>(first.size()) == adapter.dimension(),
              "simple local embedding adapter should emit vectors for UTF-8 text");
  assert_true(first == second,
              "simple local embedding adapter should keep UTF-8 embeddings deterministic");
  assert_true(first != different,
              "simple local embedding adapter should distinguish different UTF-8 token sets");

  const float squared_norm = std::inner_product(first.begin(), first.end(), first.begin(), 0.0F);
  assert_true(std::fabs(squared_norm - 1.0F) < 0.001F,
              "simple local embedding adapter should L2-normalize UTF-8 embeddings");
}

}  // namespace

int main() {
  try {
    test_simple_local_embedding_adapter_is_deterministic_and_normalized();
    test_simple_local_embedding_adapter_handles_utf8_text();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}