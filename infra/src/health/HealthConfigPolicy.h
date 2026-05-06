#pragma once

#include <optional>
#include <string>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "health/HealthConfigTypes.h"

namespace dasall::infra {

struct HealthConfigPatch {
  std::optional<bool> enabled;
  std::optional<std::uint32_t> liveness_interval_ms;
  std::optional<std::uint32_t> readiness_interval_ms;
  std::optional<std::uint32_t> probe_timeout_ms;
  std::optional<std::uint32_t> degraded_threshold;
  std::optional<std::uint32_t> unhealthy_consecutive_failures;
  std::optional<std::uint32_t> history_window_size;
  std::optional<bool> event_on_transition_only;
  std::optional<bool> recovery_hint_enabled;
};

struct HealthOperationStatus {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static HealthOperationStatus success() {
    return HealthOperationStatus{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static HealthOperationStatus failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return HealthOperationStatus{
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

class HealthConfigPolicy {
 public:
  HealthConfigPolicy();
  explicit HealthConfigPolicy(HealthResolvedConfig default_config);

  [[nodiscard]] const HealthResolvedConfig& load_defaults() const;
  [[nodiscard]] HealthResolvedConfig merge(const HealthConfigPatch& profile,
                                           const HealthConfigPatch& deploy) const;
  [[nodiscard]] HealthOperationStatus validate_thresholds(
      const HealthResolvedConfig& config) const;

 private:
  static void apply_profile_patch(HealthResolvedConfig& resolved,
                                  const HealthConfigPatch& patch);
  static void apply_deploy_patch(HealthResolvedConfig& resolved,
                                 const HealthConfigPatch& patch);

  HealthResolvedConfig default_config_;
};

}  // namespace dasall::infra