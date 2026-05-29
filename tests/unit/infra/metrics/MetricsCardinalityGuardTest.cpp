#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "metrics/CardinalityGuard.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricsFacade.h"
#include "support/TestAssertions.h"

namespace {

void test_cardinality_guard_accepts_allowlisted_labels_and_normalizes_error_code() {
  using dasall::infra::metrics::CardinalityGuard;
  using dasall::infra::metrics::MetricLabels;
  using dasall::tests::support::assert_true;

  CardinalityGuard guard;
  const auto result = guard.validate_labels("metrics.samples_total",
                                            MetricLabels{
                                                .module = std::string("infra"),
                                                .stage = std::string("record"),
                                                .profile = std::string("desktop_full"),
                                                .outcome = std::string("accepted"),
                                                .error_code = std::string(),
                                            });

  assert_true(result.accepted && result.labels.error_code == "none" &&
                  guard.observed_series_count("metrics.samples_total") == 1U,
              "CardinalityGuard should accept frozen allowlist labels and normalize an empty error_code to a stable placeholder");
}

void test_cardinality_guard_accepts_optional_observability_labels_and_tracks_series() {
  using dasall::infra::metrics::CardinalityGuard;
  using dasall::infra::metrics::MetricLabels;
  using dasall::tests::support::assert_true;

  CardinalityGuard guard(1U);
  const auto first = guard.validate_labels("metrics.samples_total",
                                           MetricLabels{
                                               .module = std::string("cognition"),
                                               .stage = std::string("response"),
                                               .profile = std::string("desktop_full"),
                                               .outcome = std::string("degraded"),
                                               .error_code = std::string(),
                                               .resolved_route = std::string("mock.route.response.primary"),
                                               .failure_category = std::string("adapter_transport"),
                                               .error_type = std::string("provider"),
                                           });
  const auto second = guard.validate_labels("metrics.samples_total",
                                            MetricLabels{
                                                .module = std::string("cognition"),
                                                .stage = std::string("response"),
                                                .profile = std::string("desktop_full"),
                                                .outcome = std::string("degraded"),
                                                .error_code = std::string(),
                                                .resolved_route = std::string("mock.route.response.fallback"),
                                                .failure_category = std::string("adapter_transport"),
                                                .error_type = std::string("provider"),
                                            });

  assert_true(first.accepted &&
                  first.labels.resolved_route == "mock.route.response.primary" &&
                  first.labels.failure_category == "adapter_transport" &&
                  first.labels.error_type == "provider" &&
                  !second.accepted && guard.reject_total() == 1U,
              "CardinalityGuard should accept the optional observability labels and treat route changes as distinct metric series");
}

void test_cardinality_guard_rejects_unknown_label_keys_observably() {
  using dasall::infra::metrics::CardinalityGuard;
  using dasall::infra::metrics::MetricLabelEntry;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  CardinalityGuard guard;
  const auto result = guard.validate_labels(
      "metrics.samples_total",
      std::vector<MetricLabelEntry>{
          MetricLabelEntry{.key = "module", .value = "infra"},
          MetricLabelEntry{.key = "stage", .value = "record"},
          MetricLabelEntry{.key = "profile", .value = "desktop_full"},
          MetricLabelEntry{.key = "outcome", .value = "accepted"},
          MetricLabelEntry{.key = "error_code", .value = "none"},
          MetricLabelEntry{.key = "tenant_id", .value = "42"},
      });

  assert_true(!result.accepted && result.references_only_contract_error_types() &&
                  result.result_code ==
                      map_metrics_error_code(MetricsErrorCode::LabelCardinalityExceeded).result_code &&
                  guard.reject_total() == 1U,
              "CardinalityGuard should reject unknown label keys with the frozen label-cardinality error surface and an observable reject counter");
}

void test_cardinality_guard_rejects_new_series_after_threshold() {
  using dasall::infra::metrics::CardinalityGuard;
  using dasall::infra::metrics::MetricLabels;
  using dasall::tests::support::assert_true;

  CardinalityGuard guard(1U);
  const auto first = guard.validate_labels("metrics.samples_total",
                                           MetricLabels{
                                               .module = std::string("infra"),
                                               .stage = std::string("record"),
                                               .profile = std::string("desktop_full"),
                                               .outcome = std::string("accepted"),
                                               .error_code = std::string(),
                                           });
  const auto second = guard.validate_labels("metrics.samples_total",
                                            MetricLabels{
                                                .module = std::string("infra"),
                                                .stage = std::string("record"),
                                                .profile = std::string("desktop_full"),
                                                .outcome = std::string("rejected"),
                                                .error_code = std::string("MET_E_EXPORT_FAILURE"),
                                            });

  assert_true(first.accepted && !second.accepted && guard.reject_total() == 1U &&
                  guard.observed_series_count("metrics.samples_total") == 1U,
              "CardinalityGuard should reject a new label signature once the per-metric cardinality threshold is exhausted");
}

void test_metrics_facade_surfaces_guard_reject_total_before_aggregation() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  MetricsFacade facade(1U);
  assert_true(facade.init(MetricsProviderConfig{}).ok,
              "MetricsFacade should initialize before exercising guard integration");

  const auto meter = facade.get_meter(MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string(),
  });
  assert_true(static_cast<bool>(meter),
              "MetricsFacade should create a meter before guard integration is exercised");

  const MetricIdentity identity{
      .name = std::string("metrics.guard_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("guarded records"),
  };
  assert_true(meter->create_counter(identity).has_value(),
              "MetricsFacade should register the metric identity before guard checks");

  assert_true(meter->record(MetricSample{
                  .identity_ref = identity,
                  .value = 1.0,
                  .ts_unix_ms = 1712448000000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("record"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "MetricsFacade should accept the first label signature before the guard limit is reached");

  const auto rejected = meter->record(MetricSample{
      .identity_ref = identity,
      .value = 1.0,
      .ts_unix_ms = 1712448001000,
      .labels = MetricLabels{
          .module = std::string("infra"),
          .stage = std::string("record"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("rejected"),
          .error_code = std::string("MET_E_EXPORT_FAILURE"),
      },
  });

  const auto snapshot = facade.module_snapshot();
  const auto aggregate_snapshot = facade.aggregation_snapshot();
  const auto* aggregate = aggregate_snapshot.find(identity.name);
  assert_true(!rejected.ok && rejected.references_only_contract_error_types() &&
                  rejected.result_code ==
                      map_metrics_error_code(MetricsErrorCode::LabelCardinalityExceeded).result_code &&
                  snapshot.guard_reject_total == 1U && aggregate != nullptr &&
                  aggregate->sample_count == 1U,
              "MetricsFacade should expose guard reject counts and keep rejected samples out of AggregationEngine state");
}

}  // namespace

int main() {
  try {
    test_cardinality_guard_accepts_allowlisted_labels_and_normalizes_error_code();
        test_cardinality_guard_accepts_optional_observability_labels_and_tracks_series();
    test_cardinality_guard_rejects_unknown_label_keys_observably();
    test_cardinality_guard_rejects_new_series_after_threshold();
    test_metrics_facade_surfaces_guard_reject_total_before_aggregation();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}