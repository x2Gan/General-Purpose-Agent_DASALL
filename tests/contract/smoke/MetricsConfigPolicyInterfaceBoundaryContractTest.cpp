#include <exception>
#include <iostream>
#include <optional>
#include <type_traits>

#include "../../../infra/include/metrics/IMetricConfigPolicy.h"
#include "support/TestAssertions.h"

namespace {

void test_metric_config_policy_interface_keeps_local_types_and_contract_error_surface() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::IMetricConfigPolicy;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricLabelsNormalizationResult;
  using dasall::infra::metrics::MetricPolicyResult;

  static_assert(std::is_same_v<decltype(&IMetricConfigPolicy::validate_identity),
                               MetricPolicyResult (IMetricConfigPolicy::*)(
                                   const MetricIdentity&) const>);
  static_assert(std::is_same_v<decltype(&IMetricConfigPolicy::normalize_labels),
                               MetricLabelsNormalizationResult (IMetricConfigPolicy::*)(
                                   const MetricLabels&) const>);
  static_assert(std::is_same_v<decltype(&IMetricConfigPolicy::should_accept),
                               MetricPolicyResult (IMetricConfigPolicy::*)(
                                   const MetricLabels&) const>);
  static_assert(std::is_same_v<decltype(MetricPolicyResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(MetricPolicyResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(MetricLabelsNormalizationResult{}.labels), MetricLabels>);
}

void test_metric_config_policy_interface_keeps_allowlist_guard_binary() {
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricLabelsNormalizationResult;
  using dasall::infra::metrics::MetricPolicyResult;
  using dasall::tests::support::assert_true;

  const auto failure = MetricPolicyResult::reject(
      dasall::contracts::ResultCode::PolicyDenied,
      "labels outside the frozen allowlist must be rejected",
      "metrics.should_accept",
      "IMetricConfigPolicy");
  const auto normalization_failure = MetricLabelsNormalizationResult::failure(
      dasall::contracts::ResultCode::ValidationFieldMissing,
      "labels must keep module/stage/profile/outcome explicit",
      "metrics.normalize_labels",
      "IMetricConfigPolicy");

  assert_true(MetricLabels{
                  .module = std::string("infra"),
                  .stage = std::string("record"),
                  .profile = std::string("desktop_full"),
                  .outcome = std::string("accepted"),
                  .error_code = std::string(),
              }
                  .is_valid(),
              "MetricLabels should remain valid when confined to the frozen allowlist fields");
  assert_true(!MetricLabels{}.is_valid(),
              "MetricLabels should reject missing allowlist field placeholders");
  assert_true(!failure.accepted && failure.references_only_contract_error_types(),
              "policy rejection should remain explicit and stay inside contracts ResultCode/ErrorInfo types");
  assert_true(!normalization_failure.ok && normalization_failure.references_only_contract_error_types(),
              "normalization failure should remain explicit and stay inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_metric_config_policy_interface_keeps_local_types_and_contract_error_surface();
    test_metric_config_policy_interface_keeps_allowlist_guard_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}