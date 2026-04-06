#include "health/HealthMonitorFacade.h"

#include <algorithm>
#include <chrono>
#include <string_view>
#include <utility>

namespace dasall::infra {
namespace {

constexpr std::string_view kHealthMonitorFacadeSourceRef = "HealthMonitorFacade";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] HealthMonitorRegistrationResult make_registration_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return HealthMonitorRegistrationResult::failure(result_code,
                                                  std::move(message),
                                                  std::move(stage),
                                                  std::string(kHealthMonitorFacadeSourceRef));
}

[[nodiscard]] HealthSnapshotResult make_snapshot_failure(contracts::ResultCode result_code,
                                                         std::string message,
                                                         std::string stage) {
  return HealthSnapshotResult::failure(result_code,
                                       std::move(message),
                                       std::move(stage),
                                       std::string(kHealthMonitorFacadeSourceRef));
}

}  // namespace

HealthMonitorRegistrationResult HealthMonitorFacade::register_probe(
    const HealthProbeRegistration& registration) {
  if (!registration.is_valid()) {
    return make_registration_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "health monitor requires explicit probe_name, probe_group, and probe placeholder",
        "health.register_probe");
  }

  const auto registry_result = registry_.register_probe(registration);
  if (!registry_result.ok) {
    return HealthMonitorRegistrationResult{
        .ok = false,
        .result_code = registry_result.result_code,
        .error = registry_result.error,
        .replaced_existing = false,
    };
  }

  if (lifecycle_state_ == LifecycleState::Created) {
    lifecycle_state_ = LifecycleState::Ready;
  }

  return HealthMonitorRegistrationResult::success();
}

HealthSnapshotResult HealthMonitorFacade::evaluate_now() {
  if (lifecycle_state_ == LifecycleState::SafeObserveMode) {
    return make_snapshot_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "health monitor is in safe_observe_mode and must preserve the last snapshot",
        "health.evaluate_now");
  }

  if (registry_.size() == 0U) {
    return make_snapshot_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "health monitor requires at least one registered probe before evaluate_now",
        "health.evaluate_now");
  }

  latest_snapshot_ = build_placeholder_snapshot();
  lifecycle_state_ = LifecycleState::Ready;
  return HealthSnapshotResult::success(*latest_snapshot_);
}

HealthSnapshotResult HealthMonitorFacade::get_snapshot() const {
  if (latest_snapshot_.has_value()) {
    return HealthSnapshotResult::success(*latest_snapshot_);
  }

  if (lifecycle_state_ == LifecycleState::SafeObserveMode) {
    return make_snapshot_failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "health monitor entered safe_observe_mode before a snapshot could be preserved",
        "health.get_snapshot");
  }

  return make_snapshot_failure(
      contracts::ResultCode::ValidationFieldMissing,
      "health monitor has no snapshot before the first successful evaluate_now",
      "health.get_snapshot");
}

HealthListenerSubscriptionResult HealthMonitorFacade::subscribe(
    IHealthStateListener& listener) {
  const auto existing = std::find(listeners_.begin(), listeners_.end(), &listener);
  if (existing == listeners_.end()) {
    listeners_.push_back(&listener);
  }

  return HealthListenerSubscriptionResult::success();
}

bool HealthMonitorFacade::is_ready() const {
  return lifecycle_state_ == LifecycleState::Ready;
}

bool HealthMonitorFacade::is_in_safe_observe_mode() const {
  return lifecycle_state_ == LifecycleState::SafeObserveMode;
}

std::size_t HealthMonitorFacade::registered_probe_count() const {
  return registry_.size();
}

std::size_t HealthMonitorFacade::listener_count() const {
  return listeners_.size();
}

std::optional<std::string> HealthMonitorFacade::safe_observe_reason() const {
  return safe_observe_reason_;
}

void HealthMonitorFacade::enter_safe_observe_mode_for_test(std::string reason) {
  lifecycle_state_ = LifecycleState::SafeObserveMode;
  safe_observe_reason_ = reason.empty() ? std::optional<std::string>("scheduler fault")
                                        : std::optional<std::string>(std::move(reason));
}

HealthSnapshot HealthMonitorFacade::build_placeholder_snapshot() {
  return HealthSnapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = next_snapshot_version_++,
      .timestamp = current_time_unix_ms(),
  };
}

}  // namespace dasall::infra