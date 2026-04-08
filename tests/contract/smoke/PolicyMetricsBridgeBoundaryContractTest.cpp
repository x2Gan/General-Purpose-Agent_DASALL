#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "policy/PolicyMetricsBridge.h"
#include "support/TestAssertions.h"

namespace {

class BoundaryMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples_.push_back(sample);
    if (scripted_results_.empty()) {
      return dasall::infra::metrics::MetricsOperationStatus::success(
          "metrics://boundary-record");
    }

    auto result = scripted_results_.front();
    scripted_results_.pop_front();
    return result;
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricIdentity>&
  created_identities() const {
    return created_identities_;
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricSample>&
  recorded_samples() const {
    return recorded_samples_;
  }

 private:
  std::vector<dasall::infra::metrics::MetricIdentity> created_identities_;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples_;
  std::deque<dasall::infra::metrics::MetricsOperationStatus> scripted_results_;
};

class BoundaryProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit BoundaryProvider(std::shared_ptr<BoundaryMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://boundary-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope_ = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://boundary-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://boundary-provider-shutdown");
  }

  [[nodiscard]] const dasall::infra::metrics::MeterScope& last_scope() const {
    return last_scope_;
  }

 private:
  std::shared_ptr<BoundaryMeter> meter_;
  dasall::infra::metrics::MeterScope last_scope_{};
};

bool is_frozen_policy_metric_name(std::string_view name) {
  return name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::ReloadTotal) ||
         name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::InvalidTotal) ||
         name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::PatchTotal) ||
         name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::DenyTotal) ||
         name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::RollbackTotal) ||
         name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::ActiveGeneration) ||
         name == dasall::infra::policy::policy_metric_name(
                     dasall::infra::policy::PolicyMetricKind::SafeModeTotal);
}

void test_policy_metrics_bridge_boundary_keeps_contract_types_and_frozen_scope() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyMetricKind;
  using dasall::infra::policy::PolicyMetricSignal;
  using dasall::infra::policy::PolicyMetricsBridge;
  using dasall::infra::policy::PolicyMetricsEmitResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyMetricsEmitResult{}.status.result_code),
                               ResultCode>);
  static_assert(std::is_same_v<decltype(PolicyMetricsEmitResult{}.status.error),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(&PolicyMetricsBridge::emit),
                               PolicyMetricsEmitResult (PolicyMetricsBridge::*)(
                                   const dasall::infra::policy::PolicyMetricSignal&)>);

  auto meter = std::make_shared<BoundaryMeter>();
  auto provider = std::make_shared<BoundaryProvider>(meter);
  PolicyMetricsBridge bridge(provider, "edge_balanced");

  const auto result = bridge.emit(PolicyMetricSignal{
      .kind = PolicyMetricKind::PatchTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140803000,
      .stage = std::string("apply_patch"),
      .outcome = std::string("failure"),
      .policy_error_code = PolicyErrorCode::StoreCommitFailed,
  });

  assert_true(result.emitted,
              "policy metrics bridge boundary should accept frozen patch failure observations");
  assert_equal(std::string("infra.policy"),
               provider->last_scope().name,
               "boundary bridge should always request the frozen infra.policy meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "boundary bridge should always preserve the frozen meter scope version");
  for (const auto& identity : meter->created_identities()) {
    assert_true(is_frozen_policy_metric_name(identity.name),
                "boundary bridge should only register the seven frozen policy metric families");
    assert_true(identity.is_valid(),
                "boundary bridge should only register valid MetricIdentity objects");
  }
  assert_true(!meter->recorded_samples().empty(),
              "boundary bridge should emit at least one sample when a frozen signal is accepted");
  const auto& labels = meter->recorded_samples().back().labels;
  assert_true(labels.module == "policy",
              "boundary bridge should pin module=policy in MetricLabels");
  assert_true(dasall::infra::policy::is_policy_metric_stage(labels.stage),
              "boundary bridge should keep stage inside the frozen allowlist");
  assert_true(dasall::infra::policy::is_policy_metric_outcome(labels.outcome),
              "boundary bridge should keep outcome inside the frozen allowlist");
  assert_true(dasall::infra::policy::is_policy_metric_error_code(labels.error_code),
              "boundary bridge should keep error_code inside the frozen allowlist");
}

void test_policy_metrics_bridge_boundary_rejects_non_whitelist_stage() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyMetricKind;
  using dasall::infra::policy::PolicyMetricSignal;
  using dasall::infra::policy::PolicyMetricsBridge;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<BoundaryMeter>();
  auto provider = std::make_shared<BoundaryProvider>(meter);
  PolicyMetricsBridge bridge(provider);

  const auto result = bridge.emit(PolicyMetricSignal{
      .kind = PolicyMetricKind::DenyTotal,
      .value = 1.0,
      .ts_unix_ms = 1712140804000,
      .stage = std::string("request-7c8d56"),
      .outcome = std::string("rejected"),
      .policy_error_code = PolicyErrorCode::QueryDenied,
  });

  assert_true(!result.emitted,
              "boundary bridge should reject non-whitelist stage labels before sample emission");
  assert_true(result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "boundary bridge should normalize non-whitelist labels to MET_E_CONFIG_INVALID");
  assert_true(meter->recorded_samples().empty(),
              "boundary bridge should not emit any sample once the label contract is violated");
}

}  // namespace

int main() {
  try {
    test_policy_metrics_bridge_boundary_keeps_contract_types_and_frozen_scope();
    test_policy_metrics_bridge_boundary_rejects_non_whitelist_stage();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}