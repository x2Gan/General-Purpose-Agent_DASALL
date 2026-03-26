#pragma once

#include <optional>
#include <string>
#include <utility>

#include "HealthSnapshot.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra {

struct HealthProbeRegistration {
  std::string probe_name;
  std::string probe_group;
  std::string opaque_probe_ref;

  [[nodiscard]] bool is_valid() const {
    return !probe_name.empty() && !probe_group.empty() && !opaque_probe_ref.empty();
  }
};

struct HealthMonitorRegistrationResult {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;
  bool replaced_existing = false;

  [[nodiscard]] static HealthMonitorRegistrationResult success(
      bool replaced_existing = false) {
    return HealthMonitorRegistrationResult{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .replaced_existing = replaced_existing,
    };
  }

  [[nodiscard]] static HealthMonitorRegistrationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HealthMonitorRegistrationResult{
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
        .replaced_existing = false,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct HealthEvaluationResult {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;
  HealthSnapshot snapshot;

  [[nodiscard]] static HealthEvaluationResult success(
      HealthSnapshot snapshot = {}) {
    return HealthEvaluationResult{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .snapshot = std::move(snapshot),
    };
  }

  [[nodiscard]] static HealthEvaluationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HealthEvaluationResult{
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
    if (!error.has_value()) {
      return ok;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class IHealthMonitor {
 public:
  virtual ~IHealthMonitor() = default;

  virtual HealthMonitorRegistrationResult register_probe(
      const HealthProbeRegistration& registration) = 0;
  virtual HealthEvaluationResult evaluate() = 0;
};

}  // namespace dasall::infra