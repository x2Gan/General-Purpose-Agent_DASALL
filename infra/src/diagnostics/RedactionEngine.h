#pragma once

#include <optional>
#include <string>

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

struct RedactionOutcome {
  bool redacted = false;
  DiagnosticsSnapshot snapshot;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static RedactionOutcome success(DiagnosticsSnapshot snapshot);
  [[nodiscard]] static RedactionOutcome failure(contracts::ResultCode result_code,
                                                std::string message,
                                                std::string stage,
                                                std::string source_ref);
};

class RedactionEngine final {
 public:
  [[nodiscard]] RedactionOutcome redact(DiagnosticsSnapshot snapshot) const;
};

}  // namespace dasall::infra::diagnostics