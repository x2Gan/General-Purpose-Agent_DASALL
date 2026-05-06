#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "maintenance/BackgroundMaintenanceHooks.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"

namespace {

using dasall::runtime::BackgroundMaintenanceHookOptions;
using dasall::runtime::BackgroundMaintenanceHooks;
using dasall::runtime::BackgroundMaintenanceTick;
using dasall::runtime::RuntimeEventBus;
using dasall::runtime::RuntimeEventBusOptions;
using dasall::runtime::RuntimeEventEnvelope;

[[nodiscard]] bool has_attribute(const RuntimeEventEnvelope& envelope,
                                 const std::string& key,
                                 const std::string& value) {
  return std::find_if(
             envelope.attributes.begin(),
             envelope.attributes.end(),
             [&key, &value](const auto& attribute) {
               return attribute.key == key && attribute.value == value;
             }) != envelope.attributes.end();
}

void test_background_maintenance_hooks_publish_idle_tick_events_to_runtime_event_bus() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto event_bus = std::make_shared<RuntimeEventBus>(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 8U,
      .now_ms = []() { return 1700000003000LL; },
  });
  BackgroundMaintenanceHooks hooks(
      event_bus,
      BackgroundMaintenanceHookOptions{
          .now_ms = []() { return 1700000003001LL; },
          .event_name_prefix = "runtime.maintenance",
      });

  std::vector<RuntimeEventEnvelope> delivered;
  const auto subscription = event_bus->subscribe(
      "runtime.maintenance.idle_tick",
      [&delivered](const RuntimeEventEnvelope& event) { delivered.push_back(event); });
  assert_true(subscription.is_valid(),
              "maintenance test subscription should be valid");

  const auto publish_result = hooks.publish_idle_tick(BackgroundMaintenanceTick{
      .tick_sequence = 7U,
      .system_idle = true,
      .checkpoint_cleanup_due = true,
      .session_expiry_due = false,
      .health_probe_due = true,
      .profile_refresh_due = false,
      .telemetry_flush_due = true,
      .detail = "idle window ready for maintenance",
      .timestamp_ms = 1700000003002LL,
  });

  assert_true(publish_result.accepted,
              "publish_idle_tick should enqueue a maintenance event when an event bus exists");
  assert_equal(1,
               static_cast<int>(event_bus->dispatch_pending()),
               "dispatch_pending should deliver the maintenance event");
  assert_equal(1,
               static_cast<int>(delivered.size()),
               "maintenance subscriber should receive one idle tick event");
  assert_equal(std::string("runtime.maintenance.idle_tick"),
               delivered.front().event_name,
               "idle tick event name should follow the configured prefix");
  assert_true(has_attribute(delivered.front(), "checkpoint_cleanup_due", "true"),
              "maintenance tick should declare checkpoint cleanup work");
  assert_true(has_attribute(delivered.front(), "telemetry_flush_due", "true"),
              "maintenance tick should declare telemetry flush work");
}

void test_background_maintenance_hooks_fail_closed_without_event_bus() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::vector<RuntimeEventEnvelope> fallback_events;
  BackgroundMaintenanceHooks hooks(
      nullptr,
      BackgroundMaintenanceHookOptions{
          .now_ms = []() { return 1700000003009LL; },
          .event_name_prefix = "runtime.maintenance",
          .fallback_sink = [&fallback_events](const RuntimeEventEnvelope& event) {
            fallback_events.push_back(event);
          },
      });

  const auto publish_result = hooks.publish_idle_tick(BackgroundMaintenanceTick{
      .tick_sequence = 1U,
      .system_idle = true,
      .checkpoint_cleanup_due = false,
      .session_expiry_due = false,
      .health_probe_due = false,
      .profile_refresh_due = false,
      .telemetry_flush_due = false,
      .detail = "no-op idle tick",
      .timestamp_ms = 1700000003010LL,
  });

  assert_true(!publish_result.accepted,
              "publish_idle_tick should fail closed when no RuntimeEventBus is available");
  assert_equal(1,
               static_cast<int>(fallback_events.size()),
               "publish_idle_tick should preserve a local fallback event when no RuntimeEventBus is available");
  assert_true(has_attribute(fallback_events.front(), "health_probe_due", "false"),
              "local fallback events should preserve the idle tick health attribute set");
}

}  // namespace

int main() {
  try {
    test_background_maintenance_hooks_publish_idle_tick_events_to_runtime_event_bus();
    test_background_maintenance_hooks_fail_closed_without_event_bus();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}