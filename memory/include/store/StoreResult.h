#pragma once

#include <optional>
#include <string>
#include <utility>

#include "error/ResultCode.h"

namespace dasall::memory {

struct StoreResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<std::string> persisted_id;
  std::optional<std::string> error_message;

  [[nodiscard]] static StoreResult success(
      std::optional<std::string> persisted_id = std::nullopt) {
    return StoreResult{
        .ok = true,
        .result_code = std::nullopt,
        .persisted_id = std::move(persisted_id),
        .error_message = std::nullopt,
    };
  }

  [[nodiscard]] static StoreResult failure(contracts::ResultCode result_code,
                                           std::optional<std::string> error_message =
                                               std::nullopt) {
    return StoreResult{
        .ok = false,
        .result_code = result_code,
        .persisted_id = std::nullopt,
        .error_message = std::move(error_message),
    };
  }
};

}  // namespace dasall::memory