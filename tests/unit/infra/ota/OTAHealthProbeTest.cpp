#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "ota/OTAHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::ota::OTAStatusSnapshot make_status_snapshot(
    bool pending_confirm = false,
    std::uint32_t backlog_count = 0,
    std::optional<dasall::contracts::ResultCode> last_failure_code = std::nullopt) {
  return dasall::infra::ota::OTAStatusSnapshot{
      .last_plan_id = std::string("ota-plan-014"),
      .state = std::string("idle"),
      .active_slot = std::string("rootfs_b"),
      .pending_confirm = pending_confirm,
      .last_failure_code = last_failure_code,
      .backlog_count = backlog_count,
  };
}

class ScriptedOTAHealthSignalProvider final
    : public dasall::infra::ota::IOTAHealthSignalProvider {
 public:
  void push_sample(dasall::infra::ota::OTAHealthSample sample) {
    scripted_samples_.push_back(std::move(sample));
  }

  [[nodiscard]] dasall::infra::ota::OTAHealthSample sample(
      std::int64_t timeout_ms) override {
    observed_timeouts_ms_.push_back(timeout_ms);
    if (scripted_samples_.empty()) {
      return dasall::infra::ota::OTAHealthSample{
          .state = dasall::infra::ota::OTAHealthSampleState::Ready,
          .signals = dasall::infra::ota::OTAHealthSignals{
              .status_snapshot = make_status_snapshot(false, 0),
              .audit_bridge_degraded = false,
              .rollback_degraded = false,
              .last_detail_ref = std::string(),
          },
          .latency_ms = 4,
          .sampled_at_unix_ms = 1712509201000,
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
  std::deque<dasall::infra::ota::OTAHealthSample> scripted_samples_;
  std::vector<std::int64_t> observed_timeouts_ms_;
};

void test_ota_health_probe_exposes_frozen_descriptor() {
  using dasall::infra::ProbeCriticality;
  using dasall::infra::ota::OTAHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedOTAHealthSignalProvider>();
  OTAHealthProbe probe(provider);
  const auto& descriptor = probe.descriptor();

  assert_true(descriptor.has_required_fields(),
              "OTAHealthProbe descriptor should remain a valid ProbeDescriptor");
  assert_equal(std::string("infra.ota.status"),
               descriptor.probe_name,
               "OTAHealthProbe should keep the frozen probe_name");
  assert_equal(std::string("readiness"),
               descriptor.group,
               "OTAHealthProbe should stay in the readiness probe group");
  assert_true(descriptor.criticality == ProbeCriticality::Critical,
              "OTAHealthProbe should remain critical to OTA readiness and gate visibility");
  assert_equal(5000,
               static_cast<int>(descriptor.interval_ms),
               "OTAHealthProbe should keep interval_ms=5000");
  assert_equal(100,
               static_cast<int>(descriptor.timeout_ms),
               "OTAHealthProbe should keep timeout_ms=100");
}

void test_ota_health_signals_encode_pending_confirm_count() {
  using dasall::tests::support::assert_equal;

  const dasall::infra::ota::OTAHealthSignals signals{
      .status_snapshot = make_status_snapshot(true, 2),
      .audit_bridge_degraded = false,
      .rollback_degraded = false,
      .last_detail_ref = std::string(),
  };

  assert_equal(1,
               static_cast<int>(signals.pending_confirm_count()),
               "OTAHealthSignals should expose pending_confirm as an exact 0/1 count for later gauge wiring");
}

void test_ota_health_probe_degrades_when_pending_confirm_or_backlog_exists() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::ota::OTAHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedOTAHealthSignalProvider>();
  provider->push_sample(dasall::infra::ota::OTAHealthSample{
      .state = dasall::infra::ota::OTAHealthSampleState::Ready,
      .signals = dasall::infra::ota::OTAHealthSignals{
          .status_snapshot = make_status_snapshot(true, 2),
          .audit_bridge_degraded = false,
          .rollback_degraded = false,
          .last_detail_ref = std::string(),
      },
      .latency_ms = 5,
      .sampled_at_unix_ms = 1712509202000,
  });

  OTAHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "OTAHealthProbe should degrade while there is a pending confirm window or backlog to drain");
  assert_true(!result.error_code.has_value(),
              "pending confirm and backlog should remain observable health facts rather than probe execution failures");
  assert_equal(std::string("status://ota/health/degraded/pending_confirm/count/1/backlog/2"),
               result.detail_ref,
               "OTAHealthProbe should encode pending_confirm count and backlog count inside degraded detail_ref evidence");
}

void test_ota_health_probe_degrades_on_recent_failure_code() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::ota::OTAHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedOTAHealthSignalProvider>();
  provider->push_sample(dasall::infra::ota::OTAHealthSample{
      .state = dasall::infra::ota::OTAHealthSampleState::Ready,
      .signals = dasall::infra::ota::OTAHealthSignals{
          .status_snapshot = make_status_snapshot(false,
                                                 0,
                                                 dasall::contracts::ResultCode::ProviderTimeout),
          .audit_bridge_degraded = false,
          .rollback_degraded = false,
          .last_detail_ref = std::string(),
      },
      .latency_ms = 6,
      .sampled_at_unix_ms = 1712509203000,
  });

  OTAHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "OTAHealthProbe should degrade when OTAStatusSnapshot carries a recent failure code");
  assert_true(!result.error_code.has_value(),
              "recent OTA failures should surface as health facts rather than synthetic probe execution failures");
  assert_equal(std::string("status://ota/health/degraded/last_failure/provider"),
               result.detail_ref,
               "OTAHealthProbe should expose the last failure category in degraded detail_ref evidence");
}

void test_ota_health_probe_returns_structured_timeout_failure() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ProbeStatus;
  using dasall::infra::ota::OTAHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedOTAHealthSignalProvider>();
  provider->push_sample(dasall::infra::ota::OTAHealthSample{
      .state = dasall::infra::ota::OTAHealthSampleState::Timeout,
      .signals = {},
      .latency_ms = 100,
      .sampled_at_unix_ms = 1712509204000,
      .detail_ref = std::string("status://ota/health/timeout"),
  });

  OTAHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "OTAHealthProbe timeout should return a structured degraded ProbeResult instead of blocking OTA health evaluation");
  assert_true(result.error_code == ResultCode::ProviderTimeout,
              "OTAHealthProbe timeout should map to ProviderTimeout");
  assert_equal(std::string("status://ota/health/timeout"),
               result.detail_ref,
               "OTAHealthProbe timeout should preserve the timeout detail_ref evidence");
  assert_equal(100,
               static_cast<int>(provider->observed_timeouts_ms().front()),
               "OTAHealthProbe should pass its frozen timeout budget to the signal provider");
}

}  // namespace

int main() {
  try {
    test_ota_health_probe_exposes_frozen_descriptor();
    test_ota_health_signals_encode_pending_confirm_count();
    test_ota_health_probe_degrades_when_pending_confirm_or_backlog_exists();
    test_ota_health_probe_degrades_on_recent_failure_code();
    test_ota_health_probe_returns_structured_timeout_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}