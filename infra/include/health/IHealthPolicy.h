#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "health/HealthStateTypes.h"

namespace dasall::infra {

struct ProbeResult;

struct ProbeResultView {
  const ProbeResult* data = nullptr;
  std::size_t size = 0;

  [[nodiscard]] bool is_valid() const {
    return data != nullptr || size == 0;
  }

  [[nodiscard]] bool empty() const {
    return size == 0;
  }
};

struct HealthPolicyEvaluationResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  HealthSnapshot snapshot;

  [[nodiscard]] static HealthPolicyEvaluationResult success(HealthSnapshot snapshot = {}) {
    return HealthPolicyEvaluationResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .snapshot = std::move(snapshot),
    };
  }

  [[nodiscard]] static HealthPolicyEvaluationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HealthPolicyEvaluationResult{
        .ok = false,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.health",
                .ref_id = std::move(source_ref),
            },
        },
        .snapshot = {},
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

class IHealthPolicy {
 public:
  virtual ~IHealthPolicy() = default;

  [[nodiscard]] virtual HealthPolicyEvaluationResult evaluate(ProbeResultView results) const = 0;
  [[nodiscard]] virtual std::string_view policy_version() const = 0;
};

}  // namespace dasall::infra