#include "BackgroundMaintenanceHooks.h"

#include <chrono>

namespace dasall::runtime {
namespace {

void append_boolean_attribute(RuntimeEventEnvelope* envelope,
                              const std::string& key,
                              const bool value) {
  envelope->attributes.push_back(RuntimeEventAttribute{
      .key = key,
      .value = value ? "true" : "false",
  });
}

}  // namespace

BackgroundMaintenanceHooks::BackgroundMaintenanceHooks(
    std::shared_ptr<RuntimeEventBus> event_bus,
    BackgroundMaintenanceHookOptions options)
    : event_bus_(std::move(event_bus)), options_(std::move(options)) {
  if (!options_.now_ms) {
    options_.now_ms = []() { return default_now_ms(); };
  }
}

RuntimeEventPublishResult BackgroundMaintenanceHooks::publish_idle_tick(
    const BackgroundMaintenanceTick& tick) {
  const auto event = make_idle_tick_event(tick);
  if (event_bus_ == nullptr) {
    if (options_.fallback_sink) {
      options_.fallback_sink(event);
    }
    return RuntimeEventPublishResult{
        .accepted = false,
        .dropped_oldest = false,
        .dropped_count = 0U,
        .queue_depth = 0U,
    };
  }

  const auto publish_result = event_bus_->publish(event);
  if (!publish_result.accepted && options_.fallback_sink) {
    options_.fallback_sink(event);
  }

  return publish_result;
}

std::int64_t BackgroundMaintenanceHooks::default_now_ms() {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

RuntimeEventEnvelope BackgroundMaintenanceHooks::make_idle_tick_event(
    const BackgroundMaintenanceTick& tick) const {
  RuntimeEventEnvelope event{
      .sequence = 0U,
      .category = RuntimeEventCategory::Maintenance,
      .severity = tick.has_due_work() ? RuntimeEventSeverity::Info
                                      : RuntimeEventSeverity::Debug,
      .event_name = options_.event_name_prefix + ".idle_tick",
      .detail = tick.detail.empty() ? "idle maintenance tick" : tick.detail,
      .context = RuntimeEventContext{},
      .error_code = std::nullopt,
      .attributes = {},
      .audit = false,
      .timestamp_ms = tick.timestamp_ms > 0 ? tick.timestamp_ms : options_.now_ms(),
  };
  event.attributes.push_back(RuntimeEventAttribute{
      .key = "tick_sequence",
      .value = std::to_string(tick.tick_sequence),
  });
  append_boolean_attribute(&event, "system_idle", tick.system_idle);
  append_boolean_attribute(&event, "checkpoint_cleanup_due", tick.checkpoint_cleanup_due);
  append_boolean_attribute(&event, "session_expiry_due", tick.session_expiry_due);
  append_boolean_attribute(&event, "health_probe_due", tick.health_probe_due);
  append_boolean_attribute(&event, "profile_refresh_due", tick.profile_refresh_due);
  append_boolean_attribute(&event, "telemetry_flush_due", tick.telemetry_flush_due);
  return event;
}

}  // namespace dasall::runtime