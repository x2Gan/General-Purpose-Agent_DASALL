#include "vector/SimpleLocalEmbeddingAdapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <utility>

namespace dasall::memory {
namespace {

void flush_token(std::string& token,
                 std::vector<float>& embedding,
                 const std::hash<std::string>& hasher) {
  if (token.empty() || embedding.empty()) {
    return;
  }

  const std::size_t hash_value = hasher(token);
  const std::size_t bucket = hash_value % embedding.size();
  const float signed_weight = ((hash_value >> 8U) & 1U) == 0U ? 1.0F : -1.0F;
  embedding[bucket] += signed_weight;
  token.clear();
}

}  // namespace

SimpleLocalEmbeddingAdapter::SimpleLocalEmbeddingAdapter(int dimension)
    : dimension_(std::max(8, dimension)) {}

std::vector<float> SimpleLocalEmbeddingAdapter::embed(const std::string& text) const {
  if (text.empty()) {
    return {};
  }

  std::vector<float> embedding(static_cast<std::size_t>(dimension_), 0.0F);
  const std::hash<std::string> hasher;
  std::string token;
  token.reserve(text.size());

  for (const unsigned char ch : text) {
    if (std::isalnum(ch) != 0) {
      token.push_back(static_cast<char>(std::tolower(ch)));
      continue;
    }

    flush_token(token, embedding, hasher);
  }
  flush_token(token, embedding, hasher);

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