#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "health/HealthStateTypes.h"
#include "health/IHealthProbe.h"

namespace dasall::infra {

struct HealthTransition;

class IHealthStateListener {
 public:
  virtual ~IHealthStateListener() = default;

  virtual void on_health_transition(const HealthTransition& transition,
                                    const HealthSnapshot& snapshot) = 0;
};

struct HealthProbeRegistration {
  std::string probe_name;
  std::string probe_group;
  IHealthProbe* probe = nullptr;
  std::shared_ptr<IHealthProbe> keepalive;

  [[nodiscard]] bool is_valid() const {
    return !probe_name.empty() && !probe_group.empty() && probe != nullptr;
  }
};

struct HealthMonitorRegistrationResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  bool replaced_existing = false;

  [[nodiscard]] static HealthMonitorRegistrationResult success(
      bool replaced_existing = false) {
    return HealthMonitorRegistrationResult{
        .ok = true,
        .result_code = std::nullopt,
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
    if (!result_code.has_value() && !error.has_value()) {
      return ok;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct HealthSnapshotResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  HealthSnapshot snapshot;

  [[nodiscard]] static HealthSnapshotResult success(HealthSnapshot snapshot = {}) {
    return HealthSnapshotResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .snapshot = std::move(snapshot),
    };
  }

  [[nodiscard]] static HealthSnapshotResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HealthSnapshotResult{
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

struct HealthListenerSubscriptionResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  bool listener_registered = false;

  [[nodiscard]] static HealthListenerSubscriptionResult success() {
    return HealthListenerSubscriptionResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .listener_registered = true,
    };
  }

  [[nodiscard]] static HealthListenerSubscriptionResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HealthListenerSubscriptionResult{
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
        .listener_registered = false,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok && listener_registered;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

class IHealthMonitor {
 public:
  virtual ~IHealthMonitor() = default;

  virtual HealthMonitorRegistrationResult register_probe(
      const HealthProbeRegistration& registration) = 0;
  virtual HealthSnapshotResult evaluate_now() = 0;
  [[nodiscard]] virtual HealthSnapshotResult get_snapshot() const = 0;
  virtual HealthListenerSubscriptionResult subscribe(IHealthStateListener& listener) = 0;
};

}  // namespace dasall::infra