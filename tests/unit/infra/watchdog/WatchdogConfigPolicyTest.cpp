#include <exception>
#include <iostream>

#include "watchdog/WatchdogConfigPolicy.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_watchdog_config_policy_keeps_frozen_defaults() {
  using dasall::infra::watchdog::WatchdogConfigPolicy;
  using dasall::infra::watchdog::WatchdogEventOverflowPolicy;
  using dasall::infra::watchdog::WatchdogTimeoutLevelPolicy;
  using dasall::tests::support::assert_true;

  WatchdogConfigPolicy policy;
  const auto& config = policy.load_defaults();

  assert_true(config.enabled && config.scan_interval_ms == 500U &&
                  config.timeout_ms == 15000U && config.grace_ms == 2000U &&
                  config.consecutive_miss_threshold == 3U &&
                  config.timeout_level_policy ==
                      WatchdogTimeoutLevelPolicy::WarnThenCritical &&
                  config.event_queue_size == 2048U &&
                  config.event_overflow_policy ==
                      WatchdogEventOverflowPolicy::Block &&
                  config.recovery_hint_enabled && config.audit_required &&
                  config.max_entities == 1024U &&
                  config.safe_mode_scan_interval_ms == 2000U &&
                  policy.validate_limits(config).ok,
              "WatchdogConfigPolicy should preserve the frozen 6.9 defaults for intervals, thresholds, queue sizing, advisory output, and safe mode bounds");
}

void test_watchdog_config_policy_merges_profile_deploy_runtime_in_order() {
  using dasall::infra::watchdog::WatchdogConfigPatch;
  using dasall::infra::watchdog::WatchdogConfigPolicy;
  using dasall::infra::watchdog::WatchdogEventOverflowPolicy;
  using dasall::infra::watchdog::WatchdogTimeoutLevelPolicy;
  using dasall::tests::support::assert_true;

  WatchdogConfigPolicy policy;
  WatchdogConfigPatch profile_patch;
  profile_patch.enabled = false;
  profile_patch.scan_interval_ms = 800U;
  profile_patch.grace_ms = 2500U;
  profile_patch.timeout_level_policy =
      WatchdogTimeoutLevelPolicy::CriticalOnly;

  WatchdogConfigPatch deploy_patch;
  deploy_patch.scan_interval_ms = 1000U;
  deploy_patch.event_queue_size = 1024U;
  deploy_patch.max_entities = 512U;

  WatchdogConfigPatch runtime_patch;
  runtime_patch.enabled = true;
  runtime_patch.recovery_hint_enabled = false;
  runtime_patch.event_queue_size = 256U;
  runtime_patch.event_overflow_policy =
      WatchdogEventOverflowPolicy::Block;
  runtime_patch.safe_mode_scan_interval_ms = 3000U;

  const auto resolved =
      policy.merge_layers(profile_patch, deploy_patch, runtime_patch);

  assert_true(resolved.enabled && resolved.scan_interval_ms == 1000U &&
                  resolved.grace_ms == 2500U &&
                  resolved.timeout_level_policy ==
                      WatchdogTimeoutLevelPolicy::CriticalOnly &&
                  resolved.event_queue_size == 256U &&
                  !resolved.recovery_hint_enabled &&
                  resolved.max_entities == 512U &&
                  resolved.event_overflow_policy ==
                      WatchdogEventOverflowPolicy::Block &&
                  resolved.safe_mode_scan_interval_ms == 3000U &&
                  policy.validate_limits(resolved).ok,
              "WatchdogConfigPolicy should apply defaults -> profile -> deploy -> runtime overrides in order without losing the frozen watchdog bounds");
}

void test_watchdog_config_policy_rejects_invalid_threshold_relationships() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::WatchdogConfigPolicy;
  using dasall::tests::support::assert_true;

  WatchdogConfigPolicy policy;
  auto invalid = policy.load_defaults();
  invalid.grace_ms = invalid.timeout_ms;
  invalid.safe_mode_scan_interval_ms = invalid.scan_interval_ms - 1U;

  const auto result = policy.validate_limits(invalid);

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code.has_value() &&
                  *result.result_code == ResultCode::ValidationFieldMissing,
              "WatchdogConfigPolicy should reject configs where grace windows or safe-mode scan intervals violate the frozen watchdog bounds");
}

void test_watchdog_config_policy_rejects_unspecified_policy_enums() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::WatchdogConfigPolicy;
  using dasall::infra::watchdog::WatchdogEventOverflowPolicy;
  using dasall::infra::watchdog::WatchdogTimeoutLevelPolicy;
  using dasall::tests::support::assert_true;

  WatchdogConfigPolicy policy;
  auto invalid = policy.load_defaults();
  invalid.timeout_level_policy = WatchdogTimeoutLevelPolicy::Unspecified;
  invalid.event_overflow_policy = WatchdogEventOverflowPolicy::Unspecified;

  const auto result = policy.validate_limits(invalid);

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code.has_value() &&
                  *result.result_code == ResultCode::ValidationFieldMissing,
              "WatchdogConfigPolicy should reject timeout and overflow policies outside the frozen enum set");
}

}  // namespace

int main() {
  try {
    test_watchdog_config_policy_keeps_frozen_defaults();
    test_watchdog_config_policy_merges_profile_deploy_runtime_in_order();
    test_watchdog_config_policy_rejects_invalid_threshold_relationships();
    test_watchdog_config_policy_rejects_unspecified_policy_enums();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}