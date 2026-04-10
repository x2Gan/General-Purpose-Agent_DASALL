#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::metrics {

class IMeter;

struct MetricsProviderConfig {
  bool enabled = true;
  std::string provider_type = "internal";
  std::string exporter_type = "noop";
  std::uint32_t reader_interval_ms = 5000;
  std::uint32_t exporter_timeout_ms = 30000;

  [[nodiscard]] bool is_valid() const {
    return !provider_type.empty() && !exporter_type.empty() &&
           reader_interval_ms > 0 && exporter_timeout_ms > 0;
  }
};

struct MeterScope {
  std::string name;
  std::string version;
  std::string schema_url;

  [[nodiscard]] bool is_valid() const {
    return !name.empty();
  }
};

struct MetricsCallDeadline {
  std::uint32_t timeout_ms = 0;

  [[nodiscard]] bool is_valid() const {
    return timeout_ms > 0;
  }
};

struct MetricsOperationStatus {
  bool ok = false;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;
  std::string state_ref;

  [[nodiscard]] static MetricsOperationStatus success(std::string state_ref = {}) {
    return MetricsOperationStatus{
        .ok = true,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .state_ref = std::move(state_ref),
    };
  }

  [[nodiscard]] static MetricsOperationStatus failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return MetricsOperationStatus{
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
                .ref_type = "infra.metrics",
                .ref_id = std::move(source_ref),
            },
        },
      .state_ref = {},
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

class IMetricsProvider {
 public:
  virtual ~IMetricsProvider() = default;

  virtual MetricsOperationStatus init(const MetricsProviderConfig& config) = 0;
  [[nodiscard]] virtual std::shared_ptr<IMeter> get_meter(const MeterScope& scope) = 0;
  virtual MetricsOperationStatus force_flush(const MetricsCallDeadline& timeout) = 0;
  virtual MetricsOperationStatus shutdown(const MetricsCallDeadline& timeout) = 0;
};

}  // namespace dasall::infra::metrics