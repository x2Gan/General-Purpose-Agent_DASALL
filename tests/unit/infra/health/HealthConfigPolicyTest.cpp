#include <exception>
#include <iostream>

#include "health/HealthConfigPolicy.h"
#include "support/TestAssertions.h"

namespace {

void test_health_config_policy_keeps_frozen_defaults() {
  using dasall::infra::HealthConfigPolicy;
  using dasall::tests::support::assert_true;

  HealthConfigPolicy policy;
  const auto& config = policy.load_defaults();

  assert_true(config.enabled && config.liveness_interval_ms == 2000U &&
                  config.readiness_interval_ms == 5000U &&
                  config.probe_timeout_ms == 1000U &&
                  config.degraded_threshold == 1U &&
                  config.unhealthy_consecutive_failures == 3U &&
                  config.history_window_size == 20U &&
                  config.event_on_transition_only &&
                  config.recovery_hint_enabled &&
                  policy.validate_thresholds(config).ok,
              "HealthConfigPolicy should preserve the frozen health 6.9 defaults for cadence, thresholds, history, and fallback-related flags");
}

void test_health_config_policy_merges_profile_then_deploy_in_order() {
  using dasall::infra::HealthConfigPatch;
  using dasall::infra::HealthConfigPolicy;
  using dasall::tests::support::assert_true;

  HealthConfigPolicy policy;

  HealthConfigPatch profile_patch;
  profile_patch.enabled = false;
  profile_patch.liveness_interval_ms = 2500U;
  profile_patch.readiness_interval_ms = 6000U;
  profile_patch.probe_timeout_ms = 800U;
  profile_patch.degraded_threshold = 2U;
  profile_patch.unhealthy_consecutive_failures = 5U;
  profile_patch.history_window_size = 32U;
  profile_patch.event_on_transition_only = false;
  profile_patch.recovery_hint_enabled = false;

  HealthConfigPatch deploy_patch;
  deploy_patch.enabled = true;
  deploy_patch.readiness_interval_ms = 7000U;
  deploy_patch.probe_timeout_ms = 900U;
  deploy_patch.degraded_threshold = 3U;
  deploy_patch.unhealthy_consecutive_failures = 6U;
  deploy_patch.history_window_size = 64U;
  deploy_patch.event_on_transition_only = true;
  deploy_patch.recovery_hint_enabled = true;

  const auto resolved = policy.merge(profile_patch, deploy_patch);

  assert_true(resolved.enabled && resolved.liveness_interval_ms == 2500U &&
                  resolved.readiness_interval_ms == 7000U &&
                  resolved.probe_timeout_ms == 900U &&
                  resolved.degraded_threshold == 3U &&
                  resolved.unhealthy_consecutive_failures == 6U &&
                  resolved.history_window_size == 32U &&
                  !resolved.event_on_transition_only &&
                  resolved.recovery_hint_enabled &&
                  policy.validate_thresholds(resolved).ok,
              "HealthConfigPolicy should apply defaults -> profile -> deploy ordering while keeping profile-only fields out of deployment overrides");
}

void test_health_config_policy_rejects_invalid_threshold_relationships() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthConfigPolicy;
  using dasall::tests::support::assert_true;

  HealthConfigPolicy policy;
  auto invalid = policy.load_defaults();
  invalid.degraded_threshold = 4U;
  invalid.unhealthy_consecutive_failures = 3U;

  const auto result = policy.validate_thresholds(invalid);

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code.has_value() &&
                  *result.result_code == ResultCode::ValidationFieldMissing,
              "HealthConfigPolicy should reject degraded thresholds that exceed the unhealthy consecutive failure boundary");
}

void test_health_config_policy_rejects_timeout_exceeding_cadence() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthConfigPolicy;
  using dasall::tests::support::assert_true;

  HealthConfigPolicy policy;
  auto invalid = policy.load_defaults();
  invalid.probe_timeout_ms = invalid.liveness_interval_ms + 1U;

  const auto result = policy.validate_thresholds(invalid);

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code.has_value() &&
                  *result.result_code == ResultCode::ValidationFieldMissing,
              "HealthConfigPolicy should reject probe timeouts that exceed the merged cadence bounds");
}

}  // namespace

int main() {
  try {
    test_health_config_policy_keeps_frozen_defaults();
    test_health_config_policy_merges_profile_then_deploy_in_order();
    test_health_config_policy_rejects_invalid_threshold_relationships();
    test_health_config_policy_rejects_timeout_exceeding_cadence();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}