#include "DaemonSupervisorAdapter.h"

#include <chrono>
#include <utility>

#include "watchdog/HeartbeatSample.h"
#include "watchdog/WatchedEntityDescriptor.h"

namespace dasall::apps::daemon {

DaemonSupervisorAdapter::DaemonSupervisorAdapter(
    std::shared_ptr<dasall::infra::watchdog::IWatchdogService> watchdog_service,
    DaemonSupervisorAdapterOptions options)
    : watchdog_service_(std::move(watchdog_service)),
      options_(std::move(options)) {}

dasall::infra::watchdog::WatchdogOperationResult
DaemonSupervisorAdapter::notify_ready() {
  if (should_noop() || watchdog_registered_) {
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  const auto register_result = watchdog_service_->register_entity(make_descriptor());
  if (register_result.ok) {
    watchdog_registered_ = true;
  }

  return register_result;
}

dasall::infra::watchdog::WatchdogOperationResult
DaemonSupervisorAdapter::notify_stopping() {
  if (should_noop() || !watchdog_registered_) {
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  const auto unregister_result =
      watchdog_service_->unregister_entity(options_.watchdog_entity_id);
  if (unregister_result.ok) {
    watchdog_registered_ = false;
  }

  return unregister_result;
}

dasall::infra::watchdog::WatchdogOperationResult
DaemonSupervisorAdapter::tick_watchdog() {
  if (should_noop() || !watchdog_registered_) {
    return dasall::infra::watchdog::WatchdogOperationResult::success();
  }

  return watchdog_service_->heartbeat(make_heartbeat_sample());
}

bool DaemonSupervisorAdapter::watchdog_active() const {
  return options_.watchdog_enabled && watchdog_registered_;
}

bool DaemonSupervisorAdapter::should_noop() const {
  return !options_.watchdog_enabled || !watchdog_service_;
}

dasall::infra::watchdog::WatchedEntityDescriptor
DaemonSupervisorAdapter::make_descriptor() const {
  return dasall::infra::watchdog::WatchedEntityDescriptor{
      .entity_id = options_.watchdog_entity_id,
      .entity_type = "process",
      .owner_module = "daemon",
      .criticality = dasall::infra::watchdog::WatchdogEntityCriticality::Critical,
      .timeout_ms = options_.watchdog_timeout_ms,
      .grace_ms = options_.watchdog_grace_ms,
  };
}

dasall::infra::watchdog::HeartbeatSample
DaemonSupervisorAdapter::make_heartbeat_sample() {
  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now());
  const auto heartbeat_ts = now.time_since_epoch().count();
  const auto deadline_ts = heartbeat_ts + options_.watchdog_timeout_ms;

  return dasall::infra::watchdog::HeartbeatSample{
      .entity_id = options_.watchdog_entity_id,
      .heartbeat_ts = heartbeat_ts,
      .deadline_ts = deadline_ts,
      .seq_no = ++heartbeat_seq_no_,
  };
}

}  // namespace dasall::apps::daemon