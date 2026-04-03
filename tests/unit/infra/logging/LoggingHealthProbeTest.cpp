#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "logging/LoggingHealthProbe.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class ScriptedLoggingHealthSignalProvider final
    : public dasall::infra::logging::ILoggingHealthSignalProvider {
 public:
  void push_sample(dasall::infra::logging::LoggingHealthSample sample) {
    scripted_samples_.push_back(std::move(sample));
  }

  [[nodiscard]] dasall::infra::logging::LoggingHealthSample sample(
      std::int64_t timeout_ms) override {
    observed_timeouts_ms_.push_back(timeout_ms);
    if (scripted_samples_.empty()) {
      return dasall::infra::logging::LoggingHealthSample{
          .state = dasall::infra::logging::LoggingHealthSampleState::Ready,
          .signals = dasall::infra::logging::LoggingHealthSignals{
              .queue_depth = 0,
              .queue_high_watermark = 8,
              .dropped_total_delta = 0,
              .recovery_degraded = false,
              .fallback_active = false,
              .unrecoverable_failure_total = 0,
              .metrics_bridge_degraded = false,
          },
          .latency_ms = 3,
          .sampled_at_unix_ms = 1712140800000,
      };
    }

    auto sample = scripted_samples_.front();
    scripted_samples_.pop_front();
    return sample;
  }

  [[nodiscard]] const std::vector<std::int64_t>& observed_timeouts_ms() const {
    return observed_timeouts_ms_;
  }

 private:
  std::deque<dasall::infra::logging::LoggingHealthSample> scripted_samples_;
  std::vector<std::int64_t> observed_timeouts_ms_;
};

[[nodiscard]] dasall::infra::logging::LoggingHealthSample make_ready_sample() {
  return dasall::infra::logging::LoggingHealthSample{
      .state = dasall::infra::logging::LoggingHealthSampleState::Ready,
      .signals = dasall::infra::logging::LoggingHealthSignals{
          .queue_depth = 0,
          .queue_high_watermark = 8,
          .dropped_total_delta = 0,
          .recovery_degraded = false,
          .fallback_active = false,
          .unrecoverable_failure_total = 0,
          .metrics_bridge_degraded = false,
      },
      .latency_ms = 4,
      .sampled_at_unix_ms = 1712140801000,
  };
}

void test_logging_health_probe_exposes_frozen_descriptor() {
  using dasall::infra::ProbeCriticality;
  using dasall::infra::logging::LoggingHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedLoggingHealthSignalProvider>();
  LoggingHealthProbe probe(provider);
  const auto& descriptor = probe.descriptor();

  assert_true(descriptor.has_required_fields(),
              "logging health probe descriptor should remain a valid health ProbeDescriptor");
  assert_equal(std::string("infra.logging.pipeline"),
               descriptor.probe_name,
               "logging health probe should keep the frozen probe_name");
  assert_equal(std::string("readiness"),
               descriptor.group,
               "logging health probe should remain in the readiness probe group");
  assert_true(descriptor.criticality == ProbeCriticality::Critical,
              "logging health probe should keep criticality=Critical");
  assert_equal(5000,
               static_cast<int>(descriptor.interval_ms),
               "logging health probe should keep interval_ms=5000");
  assert_equal(100,
               static_cast<int>(descriptor.timeout_ms),
               "logging health probe should keep timeout_ms=100");
}

void test_logging_health_probe_maps_healthy_state_without_error_code() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::logging::LoggingHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedLoggingHealthSignalProvider>();
  provider->push_sample(make_ready_sample());

  LoggingHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Healthy && result.has_consistent_state(),
              "logging health probe should report Healthy when queue, recovery, and metrics signals are all nominal");
  assert_true(!result.error_code.has_value(),
              "healthy logging health probe results should not carry an error_code");
  assert_equal(std::string("diag://infra/logging/health/healthy"),
               result.detail_ref,
               "healthy logging health probe results should still expose the frozen detail_ref namespace");
  assert_equal(100,
               static_cast<int>(provider->observed_timeouts_ms().front()),
               "logging health probe should pass its frozen timeout budget to the signal provider");
}

void test_logging_health_probe_maps_degraded_when_queue_crosses_high_watermark() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::logging::LoggingHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedLoggingHealthSignalProvider>();
  auto sample = make_ready_sample();
  sample.signals.queue_depth = 8;
  sample.signals.queue_high_watermark = 8;
  provider->push_sample(sample);

  LoggingHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "logging health probe should degrade when queue depth reaches the injected high-water threshold");
  assert_true(!result.error_code.has_value(),
              "degraded state caused by logging pressure signals should remain observable without introducing a synthetic error code");
  assert_equal(std::string("diag://infra/logging/health/degraded/queue_depth"),
               result.detail_ref,
               "queue-pressure degradation should stay inside the frozen logging health detail_ref namespace");
}

void test_logging_health_probe_maps_unhealthy_when_unrecoverable_failure_exists() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::logging::LoggingHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedLoggingHealthSignalProvider>();
  auto sample = make_ready_sample();
  sample.signals.unrecoverable_failure_total = 1;
  sample.signals.fallback_active = true;
  provider->push_sample(sample);

  LoggingHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Unhealthy && result.has_consistent_state(),
              "logging health probe should escalate to Unhealthy when unrecoverable failures are still present");
  assert_true(!result.error_code.has_value(),
              "unhealthy state caused by unrecoverable logging failures should remain a signal-mapped state rather than a probe execution error");
  assert_equal(std::string("diag://infra/logging/health/unhealthy/unrecoverable_failure"),
               result.detail_ref,
               "unrecoverable logging failures should resolve to the unhealthy detail_ref branch");
}

void test_logging_health_probe_returns_structured_timeout_failure() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ProbeStatus;
  using dasall::infra::logging::LoggingHealthProbe;
  using dasall::infra::logging::LoggingHealthSample;
  using dasall::infra::logging::LoggingHealthSampleState;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedLoggingHealthSignalProvider>();
  provider->push_sample(LoggingHealthSample{
      .state = LoggingHealthSampleState::Timeout,
      .signals = {},
      .latency_ms = 100,
      .sampled_at_unix_ms = 1712140802000,
      .detail_ref = std::string("diag://infra/logging/health/timeout"),
  });

  LoggingHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "logging health probe timeout should return a structured degraded ProbeResult instead of blocking the logging pipeline");
  assert_true(result.error_code == ResultCode::ProviderTimeout,
              "logging health probe timeout should map to ProviderTimeout");
  assert_equal(std::string("diag://infra/logging/health/timeout"),
               result.detail_ref,
               "logging health probe timeout should preserve the timeout detail_ref evidence");
  assert_equal(100,
               static_cast<int>(result.latency_ms),
               "logging health probe timeout should preserve the timeout latency budget in the ProbeResult");
}

}  // namespace

int main() {
  try {
    test_logging_health_probe_exposes_frozen_descriptor();
    test_logging_health_probe_maps_healthy_state_without_error_code();
    test_logging_health_probe_maps_degraded_when_queue_crosses_high_watermark();
    test_logging_health_probe_maps_unhealthy_when_unrecoverable_failure_exists();
    test_logging_health_probe_returns_structured_timeout_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}