#pragma once

#include "vector/IEmbeddingAdapter.h"

namespace dasall::memory {

class SimpleLocalEmbeddingAdapter final : public IEmbeddingAdapter {
 public:
  explicit SimpleLocalEmbeddingAdapter(int dimension = 64);

  [[nodiscard]] std::vector<float> embed(const std::string& text) const override;
  [[nodiscard]] int dimension() const override;

 private:
  int dimension_ = 64;
};

}  // namespace dasall::memory