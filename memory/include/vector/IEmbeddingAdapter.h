#pragma once

#include <string>
#include <vector>

namespace dasall::memory {

class IEmbeddingAdapter {
 public:
  virtual ~IEmbeddingAdapter() = default;

  [[nodiscard]] virtual std::vector<float> embed(const std::string& text) const = 0;
  [[nodiscard]] virtual int dimension() const = 0;
};

}  // namespace dasall::memory