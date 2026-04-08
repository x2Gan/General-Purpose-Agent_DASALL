#include "watchdog/WatchdogServiceFacade.h"

#include <chrono>
#include <string>
#include <string_view>

#include "watchdog/HeartbeatSample.h"
#include "watchdog/WatchedEntityDescriptor.h"
#include "watchdog/WatchdogSnapshot.h"

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kWatchdogFacadeSourceRef = "WatchdogServiceFacade";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

WatchdogOperationResult WatchdogServiceFacade::init(
    const WatchdogServiceConfig& config) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  if (!config.is_valid()) {
    return WatchdogOperationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog init requires a valid config with scan interval, timeout, grace, and queue bounds",
        "watchdog.init",
        std::string(kWatchdogFacadeSourceRef));
  }

  last_config_ = config;
  lifecycle_state_ = LifecycleState::Initialized;
  last_stop_timeout_ms_.reset();
  refresh_snapshot();
  return WatchdogOperationResult::success();
}

WatchdogOperationResult WatchdogServiceFacade::start() {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return invalid_transition("start", "initialized");
  }

  lifecycle_state_ = LifecycleState::Started;
  refresh_snapshot();
  return WatchdogOperationResult::success();
}

WatchdogOperationResult WatchdogServiceFacade::stop(std::uint32_t timeout_ms) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return invalid_transition("stop", "started");
  }

  if (timeout_ms == 0U) {
    return WatchdogOperationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog stop timeout must be greater than zero",
        "watchdog.stop",
        std::string(kWatchdogFacadeSourceRef));
  }

  last_stop_timeout_ms_ = timeout_ms;
  lifecycle_state_ = LifecycleState::Stopped;
  refresh_snapshot();
  return WatchdogOperationResult::success();
}

WatchdogOperationResult WatchdogServiceFacade::register_entity(
    const WatchedEntityDescriptor& descriptor) {
  if (lifecycle_state_ == LifecycleState::Created) {
    return invalid_transition("register_entity", "initialized");
  }

  if (!descriptor.has_required_fields()) {
    return WatchdogOperationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog registration requires explicit entity metadata and timeout bounds",
        "watchdog.register_entity",
        std::string(kWatchdogFacadeSourceRef));
  }

  return component_not_ready("register_entity", "HeartbeatRegistry");
}

WatchdogOperationResult WatchdogServiceFacade::unregister_entity(
    std::string_view entity_id) {
  if (lifecycle_state_ == LifecycleState::Created) {
    return invalid_transition("unregister_entity", "initialized");
  }

  if (entity_id.empty()) {
    return WatchdogOperationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog unregister_entity requires a non-empty entity_id",
        "watchdog.unregister_entity",
        std::string(kWatchdogFacadeSourceRef));
  }

  return component_not_ready("unregister_entity", "HeartbeatRegistry");
}

WatchdogOperationResult WatchdogServiceFacade::heartbeat(
    const HeartbeatSample& sample) {
  if (lifecycle_state_ != LifecycleState::Started) {
    return invalid_transition("heartbeat", "started");
  }

  if (!sample.has_required_fields()) {
    return WatchdogOperationResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog heartbeat requires entity_id, timestamps, and seq_no",
        "watchdog.heartbeat",
        std::string(kWatchdogFacadeSourceRef));
  }

  return component_not_ready("heartbeat", "HeartbeatIngestor");
}

WatchdogSnapshotQueryResult WatchdogServiceFacade::snapshot() const {
  if (!latest_snapshot_) {
    return WatchdogSnapshotQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog has no snapshot before init succeeds",
        "watchdog.snapshot",
        std::string(kWatchdogFacadeSourceRef));
  }

  return WatchdogSnapshotQueryResult::success(latest_snapshot_);
}

std::string_view WatchdogServiceFacade::lifecycle_state_name() const {
  switch (lifecycle_state_) {
    case LifecycleState::Created:
      return "created";
    case LifecycleState::Initialized:
      return "initialized";
    case LifecycleState::Started:
      return "started";
    case LifecycleState::Stopped:
      return "stopped";
  }

  return "unknown";
}

bool WatchdogServiceFacade::has_snapshot() const {
  return latest_snapshot_ != nullptr;
}

std::optional<std::uint32_t> WatchdogServiceFacade::last_stop_timeout_ms() const {
  return last_stop_timeout_ms_;
}

WatchdogOperationResult WatchdogServiceFacade::invalid_transition(
    std::string_view operation,
    std::string_view expected_state) const {
  return WatchdogOperationResult::failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "invalid watchdog lifecycle transition for operation " +
          std::string(operation) + ": expected state " +
          std::string(expected_state) + ", actual state " +
          std::string(lifecycle_state_name()),
      "watchdog.lifecycle",
      std::string(kWatchdogFacadeSourceRef));
}

WatchdogOperationResult WatchdogServiceFacade::component_not_ready(
    std::string_view operation,
    std::string_view component) const {
  return WatchdogOperationResult::failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "watchdog operation " + std::string(operation) +
          " is blocked until " + std::string(component) + " is wired",
      "watchdog.component_not_ready",
      std::string(kWatchdogFacadeSourceRef));
}

void WatchdogServiceFacade::refresh_snapshot() {
  auto snapshot = std::make_shared<WatchdogSnapshot>();
  snapshot->version = next_snapshot_version_++;
  snapshot->total_entities = 0;
  snapshot->timed_out_entities = 0;
  snapshot->degraded_entities = 0;
  snapshot->scan_lag_ms = 0;
  snapshot->ts = current_time_unix_ms();
  latest_snapshot_ = std::move(snapshot);
}

}  // namespace dasall::infra::watchdog