#pragma once

#include <optional>
#include <string>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::metrics {

struct MetricPolicyResult {
  bool accepted = false;
  contracts::ResultCode result_code = contracts::ResultCode::PolicyDenied;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static MetricPolicyResult accept() {
    return MetricPolicyResult{
        .accepted = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static MetricPolicyResult reject(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return MetricPolicyResult{
        .accepted = false,
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
                .ref_type = "infra.metrics",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return accepted;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct MetricLabelsNormalizationResult {
  bool ok = false;
  MetricLabels labels;
  contracts::ResultCode result_code = contracts::ResultCode::PolicyDenied;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static MetricLabelsNormalizationResult success(MetricLabels labels) {
    return MetricLabelsNormalizationResult{
        .ok = true,
        .labels = std::move(labels),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static MetricLabelsNormalizationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return MetricLabelsNormalizationResult{
        .ok = false,
        .labels = {},
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
                .ref_type = "infra.metrics",
                .ref_id = std::move(source_ref),
            },
        },
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

class IMetricConfigPolicy {
 public:
  virtual ~IMetricConfigPolicy() = default;

  [[nodiscard]] virtual MetricPolicyResult validate_identity(const MetricIdentity& identity) const = 0;
  [[nodiscard]] virtual MetricLabelsNormalizationResult normalize_labels(
      const MetricLabels& labels) const = 0;
  [[nodiscard]] virtual MetricPolicyResult should_accept(const MetricLabels& labels) const = 0;
};

}  // namespace dasall::infra::metrics