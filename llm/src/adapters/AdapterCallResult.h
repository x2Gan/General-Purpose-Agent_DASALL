#pragma once

#include <optional>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMResponse.h"

namespace dasall::llm {

// AdapterCallResult keeps provider transport/protocol failure facts inside llm
// and avoids exception-based error propagation across the adapter SPI.
struct AdapterCallResult {
  std::optional<contracts::LLMResponse> response;
  std::optional<contracts::ErrorInfo> error;
  std::optional<contracts::ResultCode> result_code;

  [[nodiscard]] bool has_consistent_values() const {
    if (response.has_value() == error.has_value()) {
      return false;
    }

    if (error.has_value() != result_code.has_value()) {
      return false;
    }

    return true;
  }
};

}  // namespace dasall::llm
