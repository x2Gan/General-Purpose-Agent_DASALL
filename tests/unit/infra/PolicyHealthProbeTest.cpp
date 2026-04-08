#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "policy/PolicyHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule() {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("policy-health-rule"),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("infra.policy"),
      .action = std::string("apply_patch"),
      .target_selector = std::string("policy:runtime_patch"),
      .effect = dasall::infra::policy::PolicyEffect::RequireConfirmation,
      .priority = 1,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("safe_mode_threshold=3")},
      .reason_code = std::string("policy_patch_confirmation_required"),
  };
}

dasall::infra::policy::PolicySnapshot make_snapshot(std::uint64_t generation) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("policy-snapshot-") + std::to_string(generation),
      .generation = generation,
      .version = std::string("1"),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {make_rule()},
      .created_at = std::string("2026-04-05T23:10:00Z"),
      .source_chain = {std::string("source_id=defaults;version=defaults@resolved")},
      .last_known_good_ref = std::string("policy-snapshot-") +
                             std::to_string(generation),
  };
}

class ScriptedPolicyHealthSignalProvider final
    : public dasall::infra::policy::IPolicyHealthSignalProvider {
 public:
  void push_sample(dasall::infra::policy::PolicyHealthSample sample) {
    scripted_samples_.push_back(std::move(sample));
  }

  [[nodiscard]] dasall::infra::policy::PolicyHealthSample sample(
      std::int64_t timeout_ms) override {
    observed_timeouts_ms_.push_back(timeout_ms);
    if (scripted_samples_.empty()) {
      return dasall::infra::policy::PolicyHealthSample{
          .state = dasall::infra::policy::PolicyHealthSampleState::Ready,
          .signals = dasall::infra::policy::PolicyHealthSignals{
              .current_snapshot = make_snapshot(3),
              .last_known_good_snapshot = make_snapshot(2),
              .safe_mode = false,
              .consecutive_patch_failures = 0,
              .audit_bridge_degraded = false,
              .metrics_bridge_degraded = false,
              .last_policy_error_code = std::nullopt,
              .last_failure_reason = std::string(),
          },
          .latency_ms = 4,
          .sampled_at_unix_ms = 1712140801000,
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
  std::deque<dasall::infra::policy::PolicyHealthSample> scripted_samples_;
  std::vector<std::int64_t> observed_timeouts_ms_;
};

[[nodiscard]] dasall::infra::policy::PolicyHealthSample make_ready_sample() {
  return dasall::infra::policy::PolicyHealthSample{
      .state = dasall::infra::policy::PolicyHealthSampleState::Ready,
      .signals = dasall::infra::policy::PolicyHealthSignals{
          .current_snapshot = make_snapshot(7),
          .last_known_good_snapshot = make_snapshot(6),
          .safe_mode = false,
          .consecutive_patch_failures = 0,
          .audit_bridge_degraded = false,
          .metrics_bridge_degraded = false,
          .last_policy_error_code = std::nullopt,
          .last_failure_reason = std::string(),
      },
      .latency_ms = 5,
      .sampled_at_unix_ms = 1712140802000,
  };
}

void test_policy_health_probe_exposes_frozen_descriptor() {
  using dasall::infra::ProbeCriticality;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedPolicyHealthSignalProvider>();
  PolicyHealthProbe probe(provider);
  const auto& descriptor = probe.descriptor();

  assert_true(descriptor.has_required_fields(),
              "policy health probe descriptor should remain a valid ProbeDescriptor");
  assert_equal(std::string("infra.policy.snapshot"),
               descriptor.probe_name,
               "policy health probe should keep the frozen probe_name");
  assert_equal(std::string("readiness"),
               descriptor.group,
               "policy health probe should stay in the readiness probe group");
  assert_true(descriptor.criticality == ProbeCriticality::Critical,
              "policy health probe should remain critical to infra readiness");
  assert_equal(5000,
               static_cast<int>(descriptor.interval_ms),
               "policy health probe should keep interval_ms=5000");
  assert_equal(100,
               static_cast<int>(descriptor.timeout_ms),
               "policy health probe should keep timeout_ms=100");
}

void test_policy_health_probe_maps_ready_state_with_generation_fact() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedPolicyHealthSignalProvider>();
  provider->push_sample(make_ready_sample());

  PolicyHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Healthy && result.has_consistent_state(),
              "policy health probe should report Healthy when an active snapshot exists and no recent failure signals are present");
  assert_true(!result.error_code.has_value(),
              "healthy policy health probe results should not carry a probe execution error_code");
  assert_equal(std::string("status://policy/health/ready/generation/7"),
               result.detail_ref,
               "policy health probe should encode the active generation inside the ready detail_ref");
  assert_equal(100,
               static_cast<int>(provider->observed_timeouts_ms().front()),
               "policy health probe should pass its frozen timeout budget to the signal provider");
}

void test_policy_health_probe_degrades_on_recent_commit_failure() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedPolicyHealthSignalProvider>();
  auto sample = make_ready_sample();
  sample.signals.last_policy_error_code = PolicyErrorCode::StoreCommitFailed;
  sample.signals.last_failure_reason = std::string("policy_store_commit_failed");
  provider->push_sample(sample);

  PolicyHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "policy health probe should degrade when the latest observed policy operation still carries a store commit failure fact");
  assert_true(!result.error_code.has_value(),
              "policy commit failures should surface as health facts instead of synthetic probe execution failures");
  assert_equal(
      std::string(
          "status://policy/health/degraded/recent_failure/INF_E_POLICY_STORE_COMMIT_FAILED/generation/7"),
      result.detail_ref,
      "policy health probe should expose the frozen policy error token and generation inside degraded detail_ref evidence");
}

void test_policy_health_probe_marks_missing_snapshot_as_unavailable() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedPolicyHealthSignalProvider>();
  provider->push_sample(dasall::infra::policy::PolicyHealthSample{
      .state = dasall::infra::policy::PolicyHealthSampleState::Ready,
      .signals = dasall::infra::policy::PolicyHealthSignals{},
      .latency_ms = 3,
      .sampled_at_unix_ms = 1712140803000,
  });

  PolicyHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Unhealthy && result.has_consistent_state(),
              "policy health probe should report unavailable when neither current nor last-known-good snapshot exists");
  assert_true(!result.error_code.has_value(),
              "missing snapshots should remain a health status fact rather than a provider execution failure");
  assert_equal(std::string("status://policy/health/unavailable/no_snapshot"),
               result.detail_ref,
               "policy health probe should preserve the unavailable/no_snapshot detail_ref branch for cold-start outages");
}

void test_policy_health_probe_returns_structured_timeout_failure() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ProbeStatus;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto provider = std::make_shared<ScriptedPolicyHealthSignalProvider>();
  provider->push_sample(dasall::infra::policy::PolicyHealthSample{
      .state = dasall::infra::policy::PolicyHealthSampleState::Timeout,
      .signals = {},
      .latency_ms = 100,
      .sampled_at_unix_ms = 1712140804000,
      .detail_ref = std::string("status://policy/health/timeout"),
  });

  PolicyHealthProbe probe(provider);
  const auto result = probe.probe();

  assert_true(result.status == ProbeStatus::Degraded && result.has_consistent_state(),
              "policy health probe timeout should return a structured degraded ProbeResult instead of blocking health evaluation");
  assert_true(result.error_code == ResultCode::ProviderTimeout,
              "policy health probe timeout should map to ProviderTimeout");
  assert_equal(std::string("status://policy/health/timeout"),
               result.detail_ref,
               "policy health probe timeout should preserve the timeout detail_ref evidence");
  assert_equal(100,
               static_cast<int>(result.latency_ms),
               "policy health probe timeout should preserve the timeout latency in ProbeResult");
}

}  // namespace

int main() {
  try {
    test_policy_health_probe_exposes_frozen_descriptor();
    test_policy_health_probe_maps_ready_state_with_generation_fact();
    test_policy_health_probe_degrades_on_recent_commit_failure();
    test_policy_health_probe_marks_missing_snapshot_as_unavailable();
    test_policy_health_probe_returns_structured_timeout_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}