#include "watchdog/WatchdogConfigPolicy.h"

#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kWatchdogConfigPolicySourceRef =
    "WatchdogConfigPolicy";

[[nodiscard]] WatchdogOperationResult make_failure(std::string message) {
  return WatchdogOperationResult::failure(
      contracts::ResultCode::ValidationFieldMissing,
      std::move(message),
      "watchdog.config.validate_limits",
      std::string(kWatchdogConfigPolicySourceRef));
}

}  // namespace

WatchdogConfigPolicy::WatchdogConfigPolicy()
    : default_config_(WatchdogServiceConfig{}) {}

WatchdogConfigPolicy::WatchdogConfigPolicy(WatchdogServiceConfig default_config)
    : default_config_(std::move(default_config)) {
  if (!default_config_.is_valid()) {
    default_config_ = WatchdogServiceConfig{};
  }
}

const WatchdogServiceConfig& WatchdogConfigPolicy::load_defaults() const {
  return default_config_;
}

WatchdogServiceConfig WatchdogConfigPolicy::merge_layers(
    const WatchdogConfigPatch& profile,
    const WatchdogConfigPatch& deploy,
    const WatchdogConfigPatch& runtime) const {
  auto resolved = default_config_;
  apply_patch(resolved, profile);
  apply_patch(resolved, deploy);
  apply_patch(resolved, runtime);
  return resolved;
}

WatchdogOperationResult WatchdogConfigPolicy::validate_limits(
    const WatchdogServiceConfig& config) const {
  if (config.scan_interval_ms == 0U) {
    return make_failure(
        "watchdog config requires infra.watchdog.scan.interval_ms > 0");
  }

  if (config.timeout_ms == 0U) {
    return make_failure("watchdog config requires infra.watchdog.timeout_ms > 0");
  }

  if (config.grace_ms >= config.timeout_ms) {
    return make_failure(
        "watchdog config requires infra.watchdog.grace_ms < infra.watchdog.timeout_ms");
  }

  if (config.consecutive_miss_threshold == 0U) {
    return make_failure(
        "watchdog config requires infra.watchdog.consecutive_miss_threshold > 0");
  }

  if (config.timeout_level_policy == WatchdogTimeoutLevelPolicy::Unspecified) {
    return make_failure(
        "watchdog config requires infra.watchdog.timeout.level.policy to stay inside the frozen enum set");
  }

  if (config.event_queue_size == 0U) {
    return make_failure(
        "watchdog config requires infra.watchdog.event.queue_size > 0");
  }

  if (config.event_overflow_policy ==
      WatchdogEventOverflowPolicy::Unspecified) {
    return make_failure(
        "watchdog config requires infra.watchdog.event.overflow_policy to stay inside the frozen enum set");
  }

  if (config.max_entities == 0U) {
    return make_failure(
        "watchdog config requires infra.watchdog.max_entities > 0");
  }

  if (config.safe_mode_scan_interval_ms < config.scan_interval_ms) {
    return make_failure(
        "watchdog config requires infra.watchdog.safe_mode.scan_interval_ms >= infra.watchdog.scan.interval_ms");
  }

  return WatchdogOperationResult::success();
}

void WatchdogConfigPolicy::apply_patch(WatchdogServiceConfig& resolved,
                                       const WatchdogConfigPatch& patch) {
  if (patch.enabled.has_value()) {
    resolved.enabled = *patch.enabled;
  }

  if (patch.scan_interval_ms.has_value()) {
    resolved.scan_interval_ms = *patch.scan_interval_ms;
  }

  if (patch.timeout_ms.has_value()) {
    resolved.timeout_ms = *patch.timeout_ms;
  }

  if (patch.grace_ms.has_value()) {
    resolved.grace_ms = *patch.grace_ms;
  }

  if (patch.consecutive_miss_threshold.has_value()) {
    resolved.consecutive_miss_threshold = *patch.consecutive_miss_threshold;
  }

  if (patch.timeout_level_policy.has_value()) {
    resolved.timeout_level_policy = *patch.timeout_level_policy;
  }

  if (patch.event_queue_size.has_value()) {
    resolved.event_queue_size = *patch.event_queue_size;
  }

  if (patch.event_overflow_policy.has_value()) {
    resolved.event_overflow_policy = *patch.event_overflow_policy;
  }

  if (patch.recovery_hint_enabled.has_value()) {
    resolved.recovery_hint_enabled = *patch.recovery_hint_enabled;
  }

  if (patch.audit_required.has_value()) {
    resolved.audit_required = *patch.audit_required;
  }

  if (patch.max_entities.has_value()) {
    resolved.max_entities = *patch.max_entities;
  }

  if (patch.safe_mode_scan_interval_ms.has_value()) {
    resolved.safe_mode_scan_interval_ms = *patch.safe_mode_scan_interval_ms;
  }
}

}  // namespace dasall::infra::watchdog