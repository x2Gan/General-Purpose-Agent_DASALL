#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "ops/ServiceHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ProbeStatus;
using dasall::services::internal::AdapterAvailabilityState;
using dasall::services::internal::IServiceHealthSignalProvider;
using dasall::services::internal::ServiceCircuitState;
using dasall::services::internal::ServiceHealthProbe;
using dasall::services::internal::ServiceHealthSample;

class StaticSignalProvider final : public IServiceHealthSignalProvider {
 public:
  explicit StaticSignalProvider(ServiceHealthSample sample)
      : sample_(std::move(sample)) {}

  [[nodiscard]] ServiceHealthSample sample(std::int64_t) override {
    return sample_;
  }

 private:
  ServiceHealthSample sample_;
};

[[nodiscard]] ServiceHealthSample make_sample() {
  ServiceHealthSample sample;
  sample.command_queue.high_watermark = 4U;
  sample.subscription_queue.high_watermark = 4U;
  sample.sampled_at_unix_ms = 1712743200000;
  sample.latency_ms = 7;
  sample.detail_ref = "status://services/health/test";
  return sample;
}

[[nodiscard]] bool has_component(const dasall::infra::HealthSnapshot& snapshot,
                                 const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

void test_service_health_probe_marks_circuit_open_as_not_ready() {
  using dasall::tests::support::assert_true;

  auto sample = make_sample();
  sample.circuit_state = ServiceCircuitState::open;

  ServiceHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();

  assert_true(result.has_consistent_state() && result.status == ProbeStatus::Degraded,
              "circuit open should surface a degraded readiness probe result");
  assert_true(snapshot.liveness && !snapshot.readiness && snapshot.degraded &&
                  has_component(snapshot, "services.circuit"),
              "circuit open should keep services live but mark readiness false with a circuit failure component");
}

void test_service_health_probe_marks_adapter_down_as_not_ready() {
  using dasall::tests::support::assert_true;

  auto sample = make_sample();
  sample.adapter_readiness = AdapterAvailabilityState::unavailable;

  ServiceHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();

  assert_true(result.status == ProbeStatus::Degraded &&
                  result.detail_ref.find("adapter_unavailable") != std::string::npos,
              "adapter down should be reflected as a degraded services readiness result");
  assert_true(snapshot.liveness && !snapshot.readiness && snapshot.degraded &&
                  has_component(snapshot, "services.adapter"),
              "adapter down should block readiness and expose the adapter component in the health snapshot");
}

void test_service_health_probe_marks_queue_overflow_as_degraded() {
  using dasall::tests::support::assert_true;

  auto sample = make_sample();
  sample.subscription_queue.depth = 4U;
  sample.subscription_queue.high_watermark = 4U;
  sample.subscription_queue.overflow_total = 1U;
  sample.subscription_queue.resync_required = true;

  ServiceHealthProbe probe(std::make_shared<StaticSignalProvider>(sample));
  const auto result = probe.probe();
  const auto snapshot = probe.snapshot();

  assert_true(result.status == ProbeStatus::Degraded &&
                  result.detail_ref.find("subscription_queue") != std::string::npos,
              "queue overflow should surface as a degraded readiness probe result");
  assert_true(snapshot.liveness && !snapshot.readiness && snapshot.degraded &&
                  has_component(snapshot, "services.subscription_queue"),
              "subscription overflow should force readiness false and expose the subscription queue component");
}

}  // namespace

int main() {
  try {
    test_service_health_probe_marks_circuit_open_as_not_ready();
    test_service_health_probe_marks_adapter_down_as_not_ready();
    test_service_health_probe_marks_queue_overflow_as_degraded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}