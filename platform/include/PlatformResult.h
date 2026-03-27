#pragma once

#include <optional>
#include <utility>

#include "PlatformError.h"

namespace dasall::platform {

template <typename TValue>
struct PlatformResult {
  std::optional<TValue> value;
  std::optional<PlatformError> error;

  [[nodiscard]] bool ok() const {
    return value.has_value() && !error.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    const bool has_value = value.has_value();
    const bool has_error = error.has_value();

    if (has_value == has_error) {
      return false;
    }

    if (has_error) {
      return error->has_consistent_values();
    }

    return true;
  }

  [[nodiscard]] static PlatformResult success(TValue result_value) {
    return PlatformResult{
        .value = std::move(result_value),
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static PlatformResult failure(PlatformError platform_error) {
    return PlatformResult{
        .value = std::nullopt,
        .error = std::move(platform_error),
    };
  }
};

}  // namespace dasall::platform