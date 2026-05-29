#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>

#include "health/RuntimeHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ProbeStatus;
using dasall::runtime::IRuntimeHealthSignalProvider;
using dasall::runtime::RuntimeHealthProbe;
using dasall::runtime::RuntimeHealthProbeOptions;
using dasall::runtime::RuntimeHealthSample;

class StaticHealthSignalProvider final : public IRuntimeHealthSignalProvider {
 public:
  explicit StaticHealthSignalProvider(RuntimeHealthSample sample)
      : sample_(std::move(sample)) {}

  RuntimeHealthSample sample(std::int64_t) override {
    return sample_;
  }

 private:
  RuntimeHealthSample sample_;
};

[[nodiscard]] bool contains_component(const dasall::infra::HealthSnapshot& snapshot,
                                      const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

void test_runtime_health_probe_reports_healthy_snapshot_when_all_signals_are_green() {
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<StaticHealthSignalProvider>(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = false,
      .event_bus_overflow = false,
      .maintenance_backlog = false,
      .safe_mode_active = false,
      .failed_components = {},
      .latency_ms = 12,
      .sampled_at_unix_ms = 1700000002000LL,
      .detail_ref = "status://runtime/health/healthy",
  });
  RuntimeHealthProbe probe(provider, RuntimeHealthProbeOptions{
                                      .detail_namespace = "status://runtime/health",
                                      .now_ms = []() { return 1700000002001LL; },
                      .logger = nullptr,
                      .runtime_instance_id = {},
                                  });

  const auto snapshot = probe.collect_snapshot();
  const auto result = probe.probe();

  assert_true(snapshot.is_ready(),
              "healthy runtime sample should aggregate to a ready health snapshot");
  assert_true(result.status == ProbeStatus::Healthy,
              "healthy runtime sample should probe as ProbeStatus::Healthy");
}

void test_runtime_health_probe_marks_degraded_components_when_event_bus_and_telemetry_are_stressed() {
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<StaticHealthSignalProvider>(RuntimeHealthSample{
      .dependencies_ready = true,
      .watchdog_healthy = true,
      .telemetry_degraded = true,
      .event_bus_overflow = true,
      .maintenance_backlog = true,
      .safe_mode_active = false,
      .failed_components = {"runtime.custom_dependency"},
      .latency_ms = 24,
      .sampled_at_unix_ms = 1700000002100LL,
      .detail_ref = "status://runtime/health/degraded",
  });
  RuntimeHealthProbe probe(provider);

  const auto snapshot = probe.collect_snapshot();
  const auto result = probe.probe();

  assert_true(snapshot.is_degraded_state(),
              "overflowed event bus and degraded telemetry should produce a degraded snapshot");
  assert_true(result.status == ProbeStatus::Degraded,
              "degraded runtime sample should probe as ProbeStatus::Degraded");
  assert_true(contains_component(snapshot, "runtime.event_bus"),
              "degraded snapshot should name runtime.event_bus as a failed component");
  assert_true(contains_component(snapshot, "runtime.telemetry"),
              "degraded snapshot should name runtime.telemetry as a failed component");
  assert_true(contains_component(snapshot, "runtime.custom_dependency"),
              "probe should preserve provider-supplied failed components");
}

void test_runtime_health_probe_fails_closed_when_signal_provider_is_missing() {
  using dasall::tests::support::assert_true;

  RuntimeHealthProbe probe(nullptr);

  const auto snapshot = probe.collect_snapshot();
  const auto result = probe.probe();

  assert_true(snapshot.is_failed_state(),
              "missing signal provider should fail closed to an unhealthy snapshot");
  assert_true(result.status == ProbeStatus::Unhealthy,
              "missing signal provider should probe as ProbeStatus::Unhealthy");
  assert_true(contains_component(snapshot, "runtime.signal_provider"),
              "missing signal provider should be surfaced in failed_components");
}

void test_runtime_health_probe_consumes_health_config_projection_for_descriptor() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::infra::HealthResolvedConfig config;
  config.readiness_interval_ms = 9000U;
  config.probe_timeout_ms = 750U;

  RuntimeHealthProbe probe(nullptr, RuntimeHealthProbeOptions{
                                        .health_config = config,
                                        .detail_namespace = "status://runtime/health",
                                        .now_ms = []() { return 1700000002200LL; },
                      .logger = nullptr,
                      .runtime_instance_id = {},
                                    });

  const auto& descriptor = probe.descriptor();

  assert_true(descriptor.has_required_fields(),
              "runtime health probe should keep a valid descriptor after cadence projection injection");
  assert_equal(9000,
               static_cast<int>(descriptor.interval_ms),
               "runtime health probe should consume readiness cadence from the health config projection");
  assert_equal(750,
               static_cast<int>(descriptor.timeout_ms),
               "runtime health probe should consume timeout budget from the health config projection");
}

}  // namespace

int main() {
  try {
    test_runtime_health_probe_reports_healthy_snapshot_when_all_signals_are_green();
    test_runtime_health_probe_marks_degraded_components_when_event_bus_and_telemetry_are_stressed();
    test_runtime_health_probe_fails_closed_when_signal_provider_is_missing();
    test_runtime_health_probe_consumes_health_config_projection_for_descriptor();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}