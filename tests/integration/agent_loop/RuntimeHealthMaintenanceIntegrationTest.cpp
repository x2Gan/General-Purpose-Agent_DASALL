#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "health/HealthMonitorFacade.h"
#include "health/RuntimeHealthProbe.h"
#include "maintenance/BackgroundMaintenanceHooks.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"

namespace {

using dasall::infra::ProbeStatus;
using dasall::runtime::BackgroundMaintenanceHookOptions;
using dasall::runtime::BackgroundMaintenanceHooks;
using dasall::runtime::BackgroundMaintenanceTick;
using dasall::runtime::IRuntimeHealthSignalProvider;
using dasall::runtime::RuntimeEventBus;
using dasall::runtime::RuntimeEventBusOptions;
using dasall::runtime::RuntimeEventEnvelope;
using dasall::runtime::RuntimeHealthProbe;
using dasall::runtime::RuntimeHealthProbeOptions;
using dasall::runtime::RuntimeHealthSample;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class MutableHealthSignalProvider final : public IRuntimeHealthSignalProvider {
 public:
  explicit MutableHealthSignalProvider(RuntimeHealthSample sample)
      : sample_(std::move(sample)) {}

  RuntimeHealthSample sample(std::int64_t) override {
    const std::lock_guard<std::mutex> lock(sample_mutex_);
    return sample_;
  }

  void set_sample(RuntimeHealthSample sample) {
    const std::lock_guard<std::mutex> lock(sample_mutex_);
    sample_ = std::move(sample);
  }

 private:
  std::mutex sample_mutex_;
  RuntimeHealthSample sample_;
};

class RecordingHealthStateListener final : public dasall::infra::IHealthStateListener {
 public:
  void on_health_transition(const dasall::infra::HealthTransition& transition,
                            const dasall::infra::HealthSnapshot& snapshot) override {
    transitions_.push_back(transition);
    snapshots_.push_back(snapshot);
  }

  [[nodiscard]] std::size_t transition_count() const {
    return transitions_.size();
  }

  [[nodiscard]] const dasall::infra::HealthTransition& last_transition() const {
    return transitions_.back();
  }

  [[nodiscard]] const dasall::infra::HealthSnapshot& last_snapshot() const {
    return snapshots_.back();
  }

 private:
  std::vector<dasall::infra::HealthTransition> transitions_;
  std::vector<dasall::infra::HealthSnapshot> snapshots_;
};

[[nodiscard]] bool contains_component(const dasall::infra::HealthSnapshot& snapshot,
                                      const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

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

void test_health_probe_tracks_maintenance_backlog_and_event_bus_pressure_until_drain() {
  auto event_bus = std::make_shared<RuntimeEventBus>(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 1U,
      .now_ms = []() { return 1700000005000LL; },
  });
  BackgroundMaintenanceHooks hooks(
      event_bus,
      BackgroundMaintenanceHookOptions{
          .now_ms = []() { return 1700000005001LL; },
          .event_name_prefix = "runtime.maintenance",
      });

  std::vector<RuntimeEventEnvelope> delivered;
  const auto subscription = event_bus->subscribe(
      "runtime.maintenance.idle_tick",
      [&delivered](const RuntimeEventEnvelope& event) { delivered.push_back(event); });
  assert_true(subscription.is_valid(),
              "health-maintenance integration should subscribe to idle tick events");

  auto provider = std::make_shared<MutableHealthSignalProvider>(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 11,
      .sampled_at_unix_ms = 1700000005002LL,
      .detail_ref = "status://runtime/health/healthy",
  });
  RuntimeHealthProbe probe(
      provider,
      RuntimeHealthProbeOptions{
          .detail_namespace = "status://runtime/health",
          .now_ms = []() { return 1700000005003LL; },
      });

  const auto first_publish = hooks.publish_idle_tick(BackgroundMaintenanceTick{
      .tick_sequence = 1U,
      .system_idle = true,
      .checkpoint_cleanup_due = true,
      .session_expiry_due = false,
      .health_probe_due = true,
      .profile_refresh_due = false,
      .telemetry_flush_due = false,
      .detail = "first idle window",
      .timestamp_ms = 1700000005004LL,
  });
  const auto second_publish = hooks.publish_idle_tick(BackgroundMaintenanceTick{
      .tick_sequence = 2U,
      .system_idle = true,
      .checkpoint_cleanup_due = true,
      .session_expiry_due = true,
      .health_probe_due = true,
      .profile_refresh_due = false,
      .telemetry_flush_due = true,
      .detail = "second idle window",
      .timestamp_ms = 1700000005005LL,
  });
  assert_true(first_publish.accepted && second_publish.accepted,
              "maintenance hooks should enqueue idle ticks on the runtime event bus");
  assert_true(second_publish.dropped_oldest,
              "maintenance overflow should drop the oldest pending non-audit event");

  provider->set_sample(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = event_bus->drop_count() > 0,
      .maintenance_backlog = event_bus->queue_depth() > 0,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 27,
      .sampled_at_unix_ms = 1700000005006LL,
      .detail_ref = "status://runtime/health/degraded/maintenance_backlog",
  });

  const auto degraded_snapshot = probe.collect_snapshot();
  const auto degraded_result = probe.probe();
  assert_true(degraded_snapshot.is_degraded_state(),
              "event bus overflow plus maintenance backlog should degrade runtime health");
  assert_true(degraded_result.status == ProbeStatus::Degraded,
              "runtime health probe should report degraded while maintenance backlog exists");
  assert_true(contains_component(degraded_snapshot, "runtime.event_bus"),
              "degraded runtime health should surface runtime.event_bus component");
  assert_true(contains_component(degraded_snapshot, "runtime.maintenance"),
              "degraded runtime health should surface runtime.maintenance component");

  assert_equal(1,
               static_cast<int>(event_bus->dispatch_pending()),
               "dispatch_pending should drain the newest idle tick without blocking the main loop");
  assert_equal(1,
               static_cast<int>(delivered.size()),
               "maintenance subscriber should only observe the retained idle tick after overflow");
  assert_true(has_attribute(delivered.front(), "tick_sequence", "2"),
              "maintenance overflow should retain the newest idle tick event");
  assert_true(has_attribute(delivered.front(), "telemetry_flush_due", "true"),
              "retained idle tick should preserve due-work attributes");

  provider->set_sample(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = event_bus->queue_depth() > 0,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 12,
      .sampled_at_unix_ms = 1700000005007LL,
      .detail_ref = "status://runtime/health/recovered",
  });

  const auto recovered_snapshot = probe.collect_snapshot();
  const auto recovered_result = probe.probe();
  assert_true(recovered_snapshot.is_ready(),
              "runtime health should recover once maintenance backlog is drained");
  assert_true(recovered_result.status == ProbeStatus::Healthy,
              "runtime health probe should return Healthy after backlog drain");
}

  void test_health_monitor_emits_runtime_transition_events_under_event_bus_backpressure() {
    auto event_bus = std::make_shared<RuntimeEventBus>(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 1U,
      .now_ms = []() { return 1700000008000LL; },
    });
    BackgroundMaintenanceHooks hooks(
      event_bus,
      BackgroundMaintenanceHookOptions{
        .now_ms = []() { return 1700000008001LL; },
        .event_name_prefix = "runtime.maintenance",
      });

    auto provider = std::make_shared<MutableHealthSignalProvider>(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 9,
      .sampled_at_unix_ms = 1700000008002LL,
      .detail_ref = "status://runtime/health/healthy",
    });
    RuntimeHealthProbe probe(
      provider,
      RuntimeHealthProbeOptions{
        .detail_namespace = "status://runtime/health",
        .now_ms = []() { return 1700000008003LL; },
      });

    dasall::infra::HealthMonitorFacade monitor;
    RecordingHealthStateListener listener;
    assert_true(monitor.subscribe(listener).ok,
          "runtime health-maintenance integration should accept a transition listener");
    assert_true(monitor.register_probe(dasall::infra::HealthProbeRegistration{
            .probe_name = std::string(dasall::runtime::kRuntimeHealthProbeName),
            .probe_group = std::string(dasall::runtime::kRuntimeHealthProbeGroup),
            .probe = &probe,
          }).ok,
          "runtime health-maintenance integration should register the runtime control-plane probe with the aggregate health monitor");

    const auto ready_snapshot = monitor.evaluate_now();
    assert_true(ready_snapshot.ok && ready_snapshot.snapshot.is_ready() &&
            listener.transition_count() == 0U,
          "runtime health-maintenance integration should not emit a transition before the first degraded state appears");

    const auto first_publish = hooks.publish_idle_tick(BackgroundMaintenanceTick{
      .tick_sequence = 1U,
      .system_idle = true,
      .checkpoint_cleanup_due = true,
      .session_expiry_due = false,
      .health_probe_due = true,
      .profile_refresh_due = false,
      .telemetry_flush_due = false,
      .detail = "health monitor idle window 1",
      .timestamp_ms = 1700000008004LL,
    });
    const auto second_publish = hooks.publish_idle_tick(BackgroundMaintenanceTick{
      .tick_sequence = 2U,
      .system_idle = true,
      .checkpoint_cleanup_due = true,
      .session_expiry_due = true,
      .health_probe_due = true,
      .profile_refresh_due = false,
      .telemetry_flush_due = true,
      .detail = "health monitor idle window 2",
      .timestamp_ms = 1700000008005LL,
    });
    assert_true(first_publish.accepted && second_publish.accepted &&
            second_publish.dropped_oldest,
          "runtime health-maintenance integration should create an event-bus overflow before the degraded aggregate evaluation");

    provider->set_sample(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = event_bus->drop_count() > 0,
      .maintenance_backlog = event_bus->queue_depth() > 0,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 21,
      .sampled_at_unix_ms = 1700000008006LL,
      .detail_ref = "status://runtime/health/degraded/event_bus",
    });

    const auto degraded_snapshot = monitor.evaluate_now();
    assert_true(degraded_snapshot.ok && degraded_snapshot.snapshot.is_degraded_state(),
          "runtime health-maintenance integration should aggregate runtime event-bus pressure into a degraded health snapshot");
    assert_true(listener.transition_count() == 1U,
          "runtime health-maintenance integration should emit exactly one transition when the runtime health state changes");
    assert_true(listener.last_transition().from_state == dasall::infra::HealthState::Healthy &&
            listener.last_transition().to_state == dasall::infra::HealthState::Degraded &&
            listener.last_transition().trigger_probe == std::string(dasall::runtime::kRuntimeHealthProbeName),
          "runtime health-maintenance integration should attribute the transition to the runtime control-plane health probe");
    assert_true(listener.last_snapshot().is_degraded_state() &&
            contains_component(listener.last_snapshot(), std::string(dasall::runtime::kRuntimeHealthProbeName)),
          "runtime health-maintenance integration should publish the degraded aggregate snapshot alongside the transition event");
  }

}  // namespace

int main() {
  try {
    test_health_probe_tracks_maintenance_backlog_and_event_bus_pressure_until_drain();
    test_health_monitor_emits_runtime_transition_events_under_event_bus_backpressure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}