#include "vector/SimpleLocalEmbeddingAdapter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::memory {
namespace {

constexpr std::uint64_t kFnv1aOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnv1aPrime = 1099511628211ULL;

[[nodiscard]] std::uint64_t stable_token_hash(std::string_view token) {
  std::uint64_t value = kFnv1aOffsetBasis;
  for (const unsigned char ch : token) {
    value ^= static_cast<std::uint64_t>(ch);
    value *= kFnv1aPrime;
  }

  return value;
}

void flush_token(std::string& token,
                 std::vector<float>& embedding) {
  if (token.empty() || embedding.empty()) {
    return;
  }

  const std::uint64_t hash_value = stable_token_hash(token);
  const std::size_t bucket = hash_value % embedding.size();
  const float signed_weight = ((hash_value >> 8U) & 1U) == 0U ? 1.0F : -1.0F;
  embedding[bucket] += signed_weight;
  token.clear();
}

[[nodiscard]] bool is_ascii_alnum(unsigned char ch) {
  return ch < 128U && std::isalnum(ch) != 0;
}

[[nodiscard]] bool is_ascii_separator(unsigned char ch) {
  return ch < 128U && std::isalnum(ch) == 0;
}

}  // namespace

SimpleLocalEmbeddingAdapter::SimpleLocalEmbeddingAdapter(int dimension)
    : dimension_(std::max(8, dimension)) {}

std::vector<float> SimpleLocalEmbeddingAdapter::embed(const std::string& text) const {
  if (text.empty()) {
    return {};
  }

  std::vector<float> embedding(static_cast<std::size_t>(dimension_), 0.0F);
  std::string token;
  token.reserve(text.size());

  for (const unsigned char ch : text) {
    if (is_ascii_alnum(ch)) {
      token.push_back(static_cast<char>(std::tolower(ch)));
      continue;
    }

    if (!is_ascii_separator(ch)) {
      token.push_back(static_cast<char>(ch));
      continue;
    }

    flush_token(token, embedding);
  }
  flush_token(token, embedding);

  const bool all_zero = std::all_of(embedding.begin(), embedding.end(), [](float value) {
    return value == 0.0F;
  });
  if (all_zero) {
    return {};
  }

  float squared_norm = 0.0F;
  for (const float value : embedding) {
    squared_norm += value * value;
  }

  const float norm = std::sqrt(squared_norm);
  if (norm <= 0.0F) {
    return {};
  }

  for (float& value : embedding) {
    value /= norm;
  }

  return embedding;
}

int SimpleLocalEmbeddingAdapter::dimension() const {
  return dimension_;
}

}  // namespace dasall::memory