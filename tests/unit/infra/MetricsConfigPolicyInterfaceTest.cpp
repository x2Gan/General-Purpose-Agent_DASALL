#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "metrics/IMetricConfigPolicy.h"
#include "support/TestAssertions.h"

namespace {

class NullMetricConfigPolicy final : public dasall::infra::metrics::IMetricConfigPolicy {
 public:
  [[nodiscard]] dasall::infra::metrics::MetricPolicyResult validate_identity(
      const dasall::infra::metrics::MetricIdentity& identity) const override {
    if (!identity.is_valid()) {
      return dasall::infra::metrics::MetricPolicyResult::reject(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metric identity must keep a valid name and unit placeholder",
          "metrics.validate_identity",
          "NullMetricConfigPolicy");
    }

    return dasall::infra::metrics::MetricPolicyResult::accept();
  }

  [[nodiscard]] dasall::infra::metrics::MetricLabelsNormalizationResult normalize_labels(
      const dasall::infra::metrics::MetricLabels& labels) const override {
    if (!labels.is_valid()) {
      return dasall::infra::metrics::MetricLabelsNormalizationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metric labels must keep module/stage/profile/outcome explicit",
          "metrics.normalize_labels",
          "NullMetricConfigPolicy");
    }

    auto normalized = labels;
    if (normalized.error_code.empty()) {
      normalized.error_code = "none";
    }

    return dasall::infra::metrics::MetricLabelsNormalizationResult::success(std::move(normalized));
  }

  [[nodiscard]] dasall::infra::metrics::MetricPolicyResult should_accept(
      const dasall::infra::metrics::MetricLabels& labels) const override {
    if (!labels.is_valid()) {
      return dasall::infra::metrics::MetricPolicyResult::reject(
          dasall::contracts::ResultCode::PolicyDenied,
          "metric labels outside the frozen allowlist must be rejected",
          "metrics.should_accept",
          "NullMetricConfigPolicy");
    }

    return dasall::infra::metrics::MetricPolicyResult::accept();
  }
};

void test_metric_config_policy_interface_accepts_valid_identity_and_labels() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  NullMetricConfigPolicy policy;
  const auto identity_result = policy.validate_identity(MetricIdentity{
      .name = std::string("metrics.export_duration_seconds"),
      .type = MetricType::Histogram,
      .unit = std::string("seconds"),
      .description = std::string("export duration"),
  });
  assert_true(identity_result.accepted,
              "IMetricConfigPolicy skeleton should accept a valid metric identity placeholder");

  const auto normalized = policy.normalize_labels(MetricLabels{
      .module = std::string("infra"),
      .stage = std::string("export"),
      .profile = std::string("desktop_full"),
      .outcome = std::string("success"),
      .error_code = std::string(),
  });
  assert_true(normalized.ok && normalized.labels.error_code == "none",
              "IMetricConfigPolicy skeleton should normalize an empty error_code to a stable placeholder");

  const auto acceptance = policy.should_accept(normalized.labels);
  assert_true(acceptance.accepted,
              "IMetricConfigPolicy skeleton should accept labels confined to the frozen allowlist fields");
}

void test_metric_config_policy_interface_reports_invalid_inputs_observably() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  NullMetricConfigPolicy policy;
  const auto invalid_identity = policy.validate_identity(MetricIdentity{
      .name = std::string("metrics invalid"),
      .type = MetricType::Counter,
      .unit = std::string(),
      .description = std::string("broken"),
  });
  assert_true(!invalid_identity.accepted,
              "IMetricConfigPolicy skeleton should reject invalid metric identity placeholders");
  assert_true(invalid_identity.references_only_contract_error_types(),
              "identity validation failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto invalid_normalized = policy.normalize_labels(MetricLabels{});
  assert_true(!invalid_normalized.ok,
              "IMetricConfigPolicy skeleton should reject incomplete label placeholders during normalization");
  assert_true(invalid_normalized.references_only_contract_error_types(),
              "label normalization failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto invalid_acceptance = policy.should_accept(MetricLabels{});
  assert_true(!invalid_acceptance.accepted,
              "IMetricConfigPolicy skeleton should reject labels outside the frozen allowlist quartet");
  assert_true(invalid_acceptance.references_only_contract_error_types(),
              "should_accept failures should stay inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_metric_config_policy_interface_accepts_valid_identity_and_labels();
    test_metric_config_policy_interface_reports_invalid_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}