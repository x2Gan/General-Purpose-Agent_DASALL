#pragma once

#include <memory>
#include <string_view>

#include "config/MemoryConfig.h"

namespace dasall::memory::util {

class ITokenEstimator {
 public:
  virtual ~ITokenEstimator() = default;

  [[nodiscard]] virtual int estimate_text_tokens(std::string_view text) const = 0;
};

class HeuristicTokenEstimator final : public ITokenEstimator {
 public:
  [[nodiscard]] int estimate_text_tokens(std::string_view text) const override;
};

[[nodiscard]] std::shared_ptr<const ITokenEstimator> create_token_estimator(
    TokenEstimatorBackend backend);

[[nodiscard]] std::shared_ptr<const ITokenEstimator> create_token_estimator(
    const MemoryConfig& config);

[[nodiscard]] int estimate_text_tokens(std::string_view text);

}  // namespace dasall::memory::util
