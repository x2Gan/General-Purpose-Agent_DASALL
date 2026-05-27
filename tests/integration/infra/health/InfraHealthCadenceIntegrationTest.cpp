#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ObservabilityLiveComposition.h"
#include "health/HealthConfigPolicy.h"
#include "health/HealthMonitorFacade.h"
#include "health/ProbeScheduler.h"
#include "health/RuntimeHealthProbe.h"
#include "maintenance/BackgroundMaintenanceHooks.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"

namespace {

using dasall::infra::HealthConfigPatch;
using dasall::infra::HealthConfigPolicy;
using dasall::infra::HealthSnapshot;
using dasall::infra::HealthTransition;
using dasall::infra::IHealthStateListener;
using dasall::infra::ProbeScheduler;
using dasall::runtime::BackgroundMaintenanceHookOptions;
using dasall::runtime::BackgroundMaintenanceHooks;
using dasall::runtime::BackgroundMaintenanceTick;
using dasall::runtime::IRuntimeHealthSignalProvider;
using dasall::runtime::RuntimeEventEnvelope;
using dasall::runtime::RuntimeHealthProbe;
using dasall::runtime::RuntimeHealthProbeOptions;
using dasall::runtime::RuntimeHealthSample;

class RecordingHealthSignalProvider final : public IRuntimeHealthSignalProvider {
 public:
  explicit RecordingHealthSignalProvider(RuntimeHealthSample sample)
      : sample_(std::move(sample)) {}

  RuntimeHealthSample sample(const std::int64_t timeout_ms) override {
    observed_timeouts_ms_.push_back(timeout_ms);
    return sample_;
  }

  [[nodiscard]] const std::vector<std::int64_t>& observed_timeouts_ms() const {
    return observed_timeouts_ms_;
  }

 private:
  RuntimeHealthSample sample_;
  std::vector<std::int64_t> observed_timeouts_ms_;
};

class RecordingHealthStateListener final : public IHealthStateListener {
 public:
  void on_health_transition(const HealthTransition& transition,
                            const HealthSnapshot& snapshot) override {
    ++notification_count_;
    last_transition_ = transition;
    last_snapshot_ = snapshot;
  }

  [[nodiscard]] std::size_t notification_count() const {
    return notification_count_;
  }

  [[nodiscard]] const std::optional<HealthTransition>& last_transition() const {
    return last_transition_;
  }

  [[nodiscard]] const std::optional<HealthSnapshot>& last_snapshot() const {
    return last_snapshot_;
  }

 private:
  std::size_t notification_count_ = 0U;
  std::optional<HealthTransition> last_transition_;
  std::optional<HealthSnapshot> last_snapshot_;
};

class TempLogRoot {
 public:
  explicit TempLogRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                                             .count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempLogRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

class AlwaysFailDispatchBackend final : public dasall::infra::logging::ILogDispatchBackend {
 public:
  dasall::infra::logging::LogWriteResult dispatch(
      const dasall::infra::logging::LogEvent&) override {
    return dasall::infra::logging::LogWriteResult::failure(
        dasall::infra::logging::map_logging_error_code(
            dasall::infra::logging::LoggingErrorCode::SinkIo)
            .result_code,
        "forced primary sink failure",
        "logging.dispatch",
        "AlwaysFailDispatchBackend");
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }
};

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

[[nodiscard]] bool snapshot_contains_component(const HealthSnapshot& snapshot,
                                               const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

[[nodiscard]] dasall::infra::logging::LogEvent make_logging_event(
    std::string message,
    std::int64_t timestamp_ms) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {{"event_name", "health.cadence.logging"}},
      .ts = timestamp_ms,
  };
}

void test_health_cadence_integration_projects_policy_into_runtime_and_preserves_fallback() {
  using dasall::infra::ProbeStatus;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  HealthConfigPolicy policy;

  HealthConfigPatch profile_patch;
  profile_patch.liveness_interval_ms = 2500U;
  profile_patch.readiness_interval_ms = 6000U;
  profile_patch.probe_timeout_ms = 800U;
  profile_patch.history_window_size = 32U;
  profile_patch.event_on_transition_only = false;

  HealthConfigPatch deploy_patch;
  deploy_patch.readiness_interval_ms = 7000U;
  deploy_patch.probe_timeout_ms = 900U;
  deploy_patch.degraded_threshold = 2U;
  deploy_patch.unhealthy_consecutive_failures = 4U;
  deploy_patch.history_window_size = 64U;
  deploy_patch.event_on_transition_only = true;

  const auto resolved = policy.merge(profile_patch, deploy_patch);
  assert_true(policy.validate_thresholds(resolved).ok &&
                  resolved.liveness_interval_ms == 2500U &&
                  resolved.readiness_interval_ms == 7000U &&
                  resolved.probe_timeout_ms == 900U &&
                  resolved.degraded_threshold == 2U &&
                  resolved.unhealthy_consecutive_failures == 4U &&
                  resolved.history_window_size == 32U &&
                  !resolved.event_on_transition_only,
              "health cadence integration should preserve the frozen profile/deploy merge semantics before runtime consumes the projection");

  auto provider = std::make_shared<RecordingHealthSignalProvider>(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 11,
      .sampled_at_unix_ms = 1700000007001LL,
      .detail_ref = "status://runtime/health/healthy",
  });
  RuntimeHealthProbe probe(provider, RuntimeHealthProbeOptions{
                                        .health_config = resolved,
                                        .detail_namespace = "status://runtime/health",
                                        .now_ms = []() { return 1700000007002LL; },
                                    });

  assert_equal(7000,
               static_cast<int>(probe.descriptor().interval_ms),
               "runtime health probe should consume readiness cadence from HealthConfigPolicy");
  assert_equal(900,
               static_cast<int>(probe.descriptor().timeout_ms),
               "runtime health probe should consume timeout budget from HealthConfigPolicy");

  std::vector<RuntimeEventEnvelope> fallback_events;
  BackgroundMaintenanceHooks hooks(
      nullptr,
      BackgroundMaintenanceHookOptions{
          .now_ms = []() { return 1700000007003LL; },
          .event_name_prefix = "runtime.maintenance",
          .fallback_sink = [&fallback_events](const RuntimeEventEnvelope& event) {
            fallback_events.push_back(event);
          },
      });

  std::vector<std::string> dispatched_groups;
  ProbeScheduler scheduler(resolved, nullptr);
  const auto started = scheduler.start([&](const std::string_view group) {
    dispatched_groups.push_back(std::string(group));
    if (group == std::string_view("readiness")) {
      const auto publish_result = hooks.publish_idle_tick(BackgroundMaintenanceTick{
          .tick_sequence = 1U,
          .system_idle = true,
          .checkpoint_cleanup_due = false,
          .session_expiry_due = false,
          .health_probe_due = true,
          .profile_refresh_due = false,
          .telemetry_flush_due = false,
          .detail = "health probe fallback",
          .timestamp_ms = 1700000007004LL,
      });
      assert_true(!publish_result.accepted,
                  "runtime maintenance hooks should stay non-bus-ready when only fallback sinks are available");
    }
  });
  const auto tick_result = scheduler.tick_once();
  const auto probe_result = probe.probe();

  assert_true(started.status.ok && !started.started && started.fallback_active &&
                  tick_result.status.ok && tick_result.triggered &&
                  tick_result.fallback_active &&
                  tick_result.dispatched_groups.size() == 2U &&
                  dispatched_groups == tick_result.dispatched_groups,
              "health cadence integration should fall back to synchronous liveness/readiness dispatch when no ITimer provider is present");
  assert_true(probe_result.status == ProbeStatus::Healthy &&
                  provider->observed_timeouts_ms().size() == 1U &&
                  provider->observed_timeouts_ms().front() == 900,
              "runtime health probe should use the merged timeout budget when consuming the health cadence projection");
  assert_true(fallback_events.size() == 1U &&
                  fallback_events.front().event_name == "runtime.maintenance.idle_tick" &&
                  has_attribute(fallback_events.front(), "health_probe_due", "true"),
              "health cadence integration should preserve maintenance fallback evidence when the event sink is absent");
}

  void test_health_cadence_integration_observes_logging_probe_transition_when_logger_degrades() {
    using dasall::infra::HealthMonitorFacade;
    using dasall::infra::ObservabilityLiveCompositionOptions;
    using dasall::infra::compose_live_observability;
    using dasall::infra::logging::LoggingFacade;
    using dasall::tests::support::assert_true;

    TempLogRoot log_root("dasall-health-cadence-logging");

    ObservabilityLiveCompositionOptions options;
    options.profile_id = "desktop_full";
    options.logging_state_root_override = log_root.path();

    const auto observability = compose_live_observability(options);
    assert_true(observability.ok(),
          "health cadence integration should compose live observability before probing logging transitions: " +
            observability.error);

    auto* facade = dynamic_cast<LoggingFacade*>(observability.logger.get());
    auto* health_monitor =
      dynamic_cast<HealthMonitorFacade*>(observability.health_monitor.get());
    assert_true(facade != nullptr && health_monitor != nullptr,
          "health cadence integration should expose concrete logging facade and health monitor types for focused transition checks");

    RecordingHealthStateListener listener;
    assert_true(health_monitor->subscribe(listener).ok,
          "health cadence integration should accept a listener before the logging probe transitions");

    const auto first_snapshot = health_monitor->evaluate_now();
    assert_true(first_snapshot.ok && first_snapshot.snapshot.is_ready(),
          "health cadence integration should establish a healthy baseline snapshot before the logging facade degrades");

    facade->set_dispatch_backend(std::make_unique<AlwaysFailDispatchBackend>());
    const auto log_result = facade->log(make_logging_event("forced degraded transition",
                               1712402001000LL));
    assert_true(log_result.ok && facade->fallback_active(),
          "forced primary sink failure should degrade through fallback while preserving the write result for the health transition check");

    const auto second_snapshot = health_monitor->evaluate_now();
    assert_true(second_snapshot.ok && second_snapshot.snapshot.is_degraded_state(),
          "health cadence integration should degrade the aggregate snapshot once the logging probe observes fallback_active=true");
    assert_true(snapshot_contains_component(second_snapshot.snapshot,
                        std::string("infra.logging.pipeline")),
          "degraded health cadence snapshot should identify infra.logging.pipeline as the failing component");
    assert_true(listener.notification_count() == 1U &&
            listener.last_transition().has_value() &&
            listener.last_transition()->trigger_probe ==
              "infra.logging.pipeline" &&
            listener.last_snapshot().has_value() &&
            listener.last_snapshot()->is_degraded_state(),
          "health cadence integration should emit a healthy-to-degraded transition once the logging readiness probe degrades");
  }

}  // namespace

int main() {
  try {
    test_health_cadence_integration_projects_policy_into_runtime_and_preserves_fallback();
    test_health_cadence_integration_observes_logging_probe_transition_when_logger_degrades();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}