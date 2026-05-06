#include "health/HealthConfigPolicy.h"

#include <string_view>
#include <utility>

namespace dasall::infra {
namespace {

constexpr std::string_view kHealthConfigPolicySourceRef = "HealthConfigPolicy";

[[nodiscard]] HealthOperationStatus make_failure(std::string message) {
  return HealthOperationStatus::failure(
      contracts::ResultCode::ValidationFieldMissing,
      std::move(message),
      "health.config.validate_thresholds",
      std::string(kHealthConfigPolicySourceRef));
}

}  // namespace

HealthConfigPolicy::HealthConfigPolicy()
    : default_config_(HealthResolvedConfig{}) {}

HealthConfigPolicy::HealthConfigPolicy(HealthResolvedConfig default_config)
    : default_config_(default_config.is_valid() ? std::move(default_config)
                                                : HealthResolvedConfig{}) {}

const HealthResolvedConfig& HealthConfigPolicy::load_defaults() const {
  return default_config_;
}

HealthResolvedConfig HealthConfigPolicy::merge(const HealthConfigPatch& profile,
                                               const HealthConfigPatch& deploy) const {
  auto resolved = default_config_;
  apply_profile_patch(resolved, profile);
  apply_deploy_patch(resolved, deploy);
  return resolved;
}

HealthOperationStatus HealthConfigPolicy::validate_thresholds(
    const HealthResolvedConfig& config) const {
  if (config.liveness_interval_ms == 0U) {
    return make_failure(
        "health config requires infra.health.liveness.interval_ms > 0");
  }

  if (config.readiness_interval_ms == 0U) {
    return make_failure(
        "health config requires infra.health.readiness.interval_ms > 0");
  }

  if (config.probe_timeout_ms == 0U) {
    return make_failure(
        "health config requires infra.health.probe.timeout_ms > 0");
  }

  if (config.probe_timeout_ms > config.liveness_interval_ms ||
      config.probe_timeout_ms > config.readiness_interval_ms) {
    return make_failure(
        "health config requires infra.health.probe.timeout_ms <= min(liveness.interval_ms, readiness.interval_ms)");
  }

  if (config.degraded_threshold == 0U) {
    return make_failure(
        "health config requires infra.health.degraded.threshold > 0");
  }

  if (config.unhealthy_consecutive_failures == 0U) {
    return make_failure(
        "health config requires infra.health.unhealthy.consecutive_failures > 0");
  }

  if (config.degraded_threshold > config.unhealthy_consecutive_failures) {
    return make_failure(
        "health config requires infra.health.degraded.threshold <= infra.health.unhealthy.consecutive_failures");
  }

  if (config.history_window_size == 0U) {
    return make_failure(
        "health config requires infra.health.history.window_size > 0");
  }

  return HealthOperationStatus::success();
}

void HealthConfigPolicy::apply_profile_patch(HealthResolvedConfig& resolved,
                                             const HealthConfigPatch& patch) {
  if (patch.enabled.has_value()) {
    resolved.enabled = *patch.enabled;
  }

  if (patch.liveness_interval_ms.has_value()) {
    resolved.liveness_interval_ms = *patch.liveness_interval_ms;
  }

  if (patch.readiness_interval_ms.has_value()) {
    resolved.readiness_interval_ms = *patch.readiness_interval_ms;
  }

  if (patch.probe_timeout_ms.has_value()) {
    resolved.probe_timeout_ms = *patch.probe_timeout_ms;
  }

  if (patch.degraded_threshold.has_value()) {
    resolved.degraded_threshold = *patch.degraded_threshold;
  }

  if (patch.unhealthy_consecutive_failures.has_value()) {
    resolved.unhealthy_consecutive_failures =
        *patch.unhealthy_consecutive_failures;
  }

  if (patch.history_window_size.has_value()) {
    resolved.history_window_size = *patch.history_window_size;
  }

  if (patch.event_on_transition_only.has_value()) {
    resolved.event_on_transition_only = *patch.event_on_transition_only;
  }

  if (patch.recovery_hint_enabled.has_value()) {
    resolved.recovery_hint_enabled = *patch.recovery_hint_enabled;
  }
}

void HealthConfigPolicy::apply_deploy_patch(HealthResolvedConfig& resolved,
                                            const HealthConfigPatch& patch) {
  if (patch.enabled.has_value()) {
    resolved.enabled = *patch.enabled;
  }

  if (patch.liveness_interval_ms.has_value()) {
    resolved.liveness_interval_ms = *patch.liveness_interval_ms;
  }

  if (patch.readiness_interval_ms.has_value()) {
    resolved.readiness_interval_ms = *patch.readiness_interval_ms;
  }

  if (patch.probe_timeout_ms.has_value()) {
    resolved.probe_timeout_ms = *patch.probe_timeout_ms;
  }

  if (patch.degraded_threshold.has_value()) {
    resolved.degraded_threshold = *patch.degraded_threshold;
  }

  if (patch.unhealthy_consecutive_failures.has_value()) {
    resolved.unhealthy_consecutive_failures =
        *patch.unhealthy_consecutive_failures;
  }

  if (patch.recovery_hint_enabled.has_value()) {
    resolved.recovery_hint_enabled = *patch.recovery_hint_enabled;
  }
}

}  // namespace dasall::infra