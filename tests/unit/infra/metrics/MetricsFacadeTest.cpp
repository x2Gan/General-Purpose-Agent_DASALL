#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "metrics/MetricsFacade.h"
#include "metrics/MetricsErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_metrics_facade_rejects_uninitialized_lifecycle_usage() {
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  MetricsFacade facade;

  const auto meter = facade.get_meter(MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  });
  assert_true(!meter,
              "MetricsFacade should reject get_meter() before init() finishes");

  const auto flush_result = facade.force_flush(MetricsCallDeadline{.timeout_ms = 100});
  assert_true(!flush_result.ok && flush_result.references_only_contract_error_types() &&
                  flush_result.result_code ==
                      map_metrics_error_code(MetricsErrorCode::ProviderNotReady).result_code,
              "MetricsFacade should expose provider-not-ready failures observably before init()");
}

void test_metrics_facade_initializes_and_records_valid_samples() {
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  MetricsFacade facade;
  const auto init_result = facade.init(MetricsProviderConfig{});
  assert_true(init_result.ok && facade.lifecycle_state_name() == "initialized",
              "MetricsFacade should enter initialized state with the frozen default provider config");

  const MeterScope scope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
  const auto first_meter = facade.get_meter(scope);
  const auto second_meter = facade.get_meter(scope);
  assert_true(first_meter && second_meter && first_meter == second_meter,
              "MetricsFacade should cache identical MeterScope requests inside the current lifecycle");

  const MetricIdentity identity{
      .name = std::string("metrics.record_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("number of recorded samples"),
  };
  const auto handle = first_meter->create_counter(identity);
  assert_true(handle.has_value() && handle->is_valid(),
              "MetricsFacade meter should materialize a stable counter handle for a valid identity");

  const auto record_result = first_meter->record(MetricSample{
      .identity_ref = identity,
      .value = 1.0,
      .ts_unix_ms = 1712361600000,
      .labels = MetricLabels{
          .module = std::string("infra"),
          .stage = std::string("record"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("accepted"),
          .error_code = std::string(),
      },
  });
  assert_true(record_result.ok && facade.record_attempt_count() == 1U &&
                  facade.last_recorded_sample().has_value() &&
                  facade.last_recorded_sample()->identity_ref.name == "metrics.record_total",
              "MetricsFacade should accept a valid sample and keep the last recorded payload observable for follow-up stages");
}

void test_metrics_facade_rejects_invalid_identity_paths_observably() {
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::infra::metrics::MetricType;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  MetricsFacade facade;
  assert_true(facade.init(MetricsProviderConfig{}).ok,
              "MetricsFacade should initialize before invalid identity failure paths are exercised");

  const auto meter = facade.get_meter(MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string(),
  });
  assert_true(static_cast<bool>(meter),
              "MetricsFacade should return a meter for a valid scope before invalid identity tests");

  const auto invalid_handle = meter->create_counter(MetricIdentity{
      .name = std::string(),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("invalid"),
  });
  assert_true(!invalid_handle.has_value(),
              "MetricsFacade meter should reject create_counter() when the metric identity is invalid");

  const auto invalid_record = meter->record(MetricSample{
      .identity_ref = MetricIdentity{
          .name = std::string(),
          .type = MetricType::Counter,
          .unit = std::string("1"),
          .description = std::string("invalid"),
      },
      .value = 1.0,
      .ts_unix_ms = 1712361600000,
      .labels = MetricLabels{
          .module = std::string("infra"),
          .stage = std::string("record"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("rejected"),
          .error_code = std::string(),
      },
  });
  assert_true(!invalid_record.ok && invalid_record.references_only_contract_error_types() &&
                  invalid_record.result_code ==
                      map_metrics_error_code(MetricsErrorCode::IdentityInvalid).result_code,
              "MetricsFacade should map invalid identity record paths to the frozen metrics identity failure surface");
}

}  // namespace

int main() {
  try {
    test_metrics_facade_rejects_uninitialized_lifecycle_usage();
    test_metrics_facade_initializes_and_records_valid_samples();
    test_metrics_facade_rejects_invalid_identity_paths_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}