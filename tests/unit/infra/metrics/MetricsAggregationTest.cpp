#include <cmath>
#include <exception>
#include <iostream>
#include <string>

#include "metrics/AggregationEngine.h"
#include "metrics/MetricsFacade.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] bool close_enough(double lhs, double rhs) {
    return std::abs(lhs - rhs) < 1e-9;
}

void test_aggregation_engine_accumulates_counter_samples() {
  using dasall::infra::metrics::AggregationEngine;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  AggregationEngine engine;
  const MetricIdentity identity{
      .name = std::string("metrics.jobs_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("processed jobs"),
  };

  assert_true(engine.aggregate_counter(MetricSample{
                  .identity_ref = identity,
                  .value = 1.0,
                  .ts_unix_ms = 1712361600000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("aggregate"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "AggregationEngine should accept the first valid counter sample");
  assert_true(engine.aggregate_counter(MetricSample{
                  .identity_ref = identity,
                  .value = 3.0,
                  .ts_unix_ms = 1712361601000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("aggregate"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "AggregationEngine should accumulate later counter samples into the same aggregate");

  const auto snapshot = engine.snapshot();
  const auto* aggregate = snapshot.find(identity.name);
    assert_true(aggregate != nullptr && aggregate->sample_count == 2U &&
                                    close_enough(aggregate->accumulated_value, 4.0) &&
                                    close_enough(aggregate->last_value, 4.0),
              "AggregationEngine should accumulate counter samples into a cumulative total snapshot");
}

void test_aggregation_engine_keeps_latest_gauge_value() {
  using dasall::infra::metrics::AggregationEngine;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  AggregationEngine engine;
  const MetricIdentity identity{
      .name = std::string("metrics.queue_depth"),
      .type = MetricType::Gauge,
      .unit = std::string("1"),
      .description = std::string("queue depth"),
  };

  assert_true(engine.aggregate_gauge(MetricSample{
                  .identity_ref = identity,
                  .value = 5.0,
                  .ts_unix_ms = 1712361600000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("aggregate"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "AggregationEngine should accept the first valid gauge sample");
  assert_true(engine.aggregate_gauge(MetricSample{
                  .identity_ref = identity,
                  .value = 2.0,
                  .ts_unix_ms = 1712361601000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("aggregate"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "AggregationEngine should accept later gauge updates");

  const auto snapshot = engine.snapshot();
  const auto* aggregate = snapshot.find(identity.name);
  assert_true(aggregate != nullptr && aggregate->sample_count == 2U &&
                  close_enough(aggregate->accumulated_value, 2.0) &&
                  close_enough(aggregate->last_value, 2.0) &&
                  close_enough(aggregate->min_value, 2.0) &&
                  close_enough(aggregate->max_value, 5.0),
              "AggregationEngine should retain the latest gauge value while preserving observed extrema");
}

void test_aggregation_engine_tracks_histogram_buckets() {
  using dasall::infra::metrics::AggregationEngine;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  AggregationEngine engine;
  const MetricIdentity identity{
      .name = std::string("metrics.request_duration"),
      .type = MetricType::Histogram,
      .unit = std::string("s"),
      .description = std::string("request duration"),
  };

  assert_true(engine.aggregate_histogram(MetricSample{
                  .identity_ref = identity,
                  .value = 0.02,
                  .ts_unix_ms = 1712361600000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("aggregate"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "AggregationEngine should accept the first histogram sample");
  assert_true(engine.aggregate_histogram(MetricSample{
                  .identity_ref = identity,
                  .value = 0.7,
                  .ts_unix_ms = 1712361601000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("aggregate"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "AggregationEngine should accept later histogram samples into the same aggregate");

  const auto snapshot = engine.snapshot();
  const auto* aggregate = snapshot.find(identity.name);
  assert_true(aggregate != nullptr && aggregate->sample_count == 2U &&
                  close_enough(aggregate->accumulated_value, 0.72) &&
                  close_enough(aggregate->min_value, 0.02) &&
                  close_enough(aggregate->max_value, 0.7) &&
                  aggregate->bucket_counts.size() == 12U &&
                  aggregate->bucket_counts[2] == 1U && aggregate->bucket_counts[8] == 1U,
              "AggregationEngine should update histogram count, sum, extrema, and explicit bucket counters deterministically");
}

void test_metrics_facade_routes_registered_samples_into_aggregation_engine() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  MetricsFacade facade;
  assert_true(facade.init(MetricsProviderConfig{}).ok,
              "MetricsFacade should initialize before aggregation bridge checks");

  const auto meter = facade.get_meter(MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string(),
  });
  assert_true(static_cast<bool>(meter),
              "MetricsFacade should materialize a meter before main-chain aggregation checks");

  const MetricIdentity identity{
      .name = std::string("metrics.pipeline_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("pipeline writes"),
  };
  assert_true(meter->create_counter(identity).has_value(),
              "MetricsFacade should register the counter identity before record() aggregation");

  assert_true(meter->record(MetricSample{
                  .identity_ref = identity,
                  .value = 2.0,
                  .ts_unix_ms = 1712361600000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("record"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "MetricsFacade should route the first registered counter sample into AggregationEngine");
  assert_true(meter->record(MetricSample{
                  .identity_ref = identity,
                  .value = 1.0,
                  .ts_unix_ms = 1712361601000,
                  .labels = MetricLabels{
                      .module = std::string("infra"),
                      .stage = std::string("record"),
                      .profile = std::string("desktop_full"),
                      .outcome = std::string("accepted"),
                      .error_code = std::string(),
                  },
              }).ok,
              "MetricsFacade should route later registered counter samples into the same aggregate");

  const auto snapshot = facade.aggregation_snapshot();
  const auto* aggregate = snapshot.find(identity.name);
  assert_true(aggregate != nullptr && aggregate->sample_count == 2U &&
                  close_enough(aggregate->accumulated_value, 3.0) &&
                  close_enough(aggregate->last_value, 3.0),
              "MetricsFacade should expose a record -> registry -> aggregation main-chain snapshot after successful writes");
}

}  // namespace

int main() {
  try {
    test_aggregation_engine_accumulates_counter_samples();
    test_aggregation_engine_keeps_latest_gauge_value();
    test_aggregation_engine_tracks_histogram_buckets();
    test_metrics_facade_routes_registered_samples_into_aggregation_engine();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}