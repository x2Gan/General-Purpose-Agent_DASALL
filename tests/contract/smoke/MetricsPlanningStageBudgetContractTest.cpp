#include <exception>
#include <iostream>
#include <string>

#include "metrics/MetricTypes.h"
#include "metrics/MetricsErrors.h"
#include "support/TestAssertions.h"

namespace {

void test_planning_stage_metrics_remain_trackable_at_contract_boundary() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::infra::metrics::is_planning_metric_name;
  using dasall::infra::metrics::is_planning_observation_sample;
  using dasall::tests::support::assert_true;

  const MetricSample planning_budget_sample{
      .identity_ref = MetricIdentity{
          .name = std::string(dasall::infra::metrics::kPlanningBudgetMetricName),
          .type = MetricType::Gauge,
          .unit = std::string("ms"),
          .description = std::string("planning-stage budget ceiling"),
      },
      .value = 250.0,
      .ts_unix_ms = 1712563200000,
      .labels = MetricLabels{
          .module = std::string("runtime"),
          .stage = std::string(dasall::infra::metrics::kPlanningMetricStageLabel),
          .profile = std::string("edge_balanced"),
          .outcome = std::string("success"),
          .error_code = std::string(),
      },
  };

  const MetricSample planning_latency_sample{
      .identity_ref = MetricIdentity{
          .name = std::string(dasall::infra::metrics::kPlanningLatencyMetricName),
          .type = MetricType::Histogram,
          .unit = std::string("ms"),
          .description = std::string("planning-stage observed latency"),
      },
      .value = 180.0,
      .ts_unix_ms = 1712563200100,
      .labels = MetricLabels{
          .module = std::string("runtime"),
          .stage = std::string(dasall::infra::metrics::kPlanningMetricStageLabel),
          .profile = std::string("edge_balanced"),
          .outcome = std::string("success"),
          .error_code = std::string(),
      },
  };

  assert_true(is_planning_metric_name(planning_budget_sample.identity_ref.name),
              "planning_budget_ms should stay inside the frozen planning-observation metric family");
  assert_true(is_planning_metric_name(planning_latency_sample.identity_ref.name),
              "planning_latency_ms should stay inside the frozen planning-observation metric family");
  assert_true(is_planning_observation_sample(planning_budget_sample),
              "planning budget samples should remain observable through stage=planning with ms budget semantics");
  assert_true(is_planning_observation_sample(planning_latency_sample),
              "planning latency samples should remain observable through stage=planning with histogram latency semantics");
}

void test_planning_stage_degraded_path_requires_explicit_error_code() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::is_planning_observation_sample;
  using dasall::infra::metrics::metrics_error_code_name;
  using dasall::tests::support::assert_true;

  const MetricSample degraded_sample{
      .identity_ref = MetricIdentity{
          .name = std::string(dasall::infra::metrics::kPlanningBudgetMetricName),
          .type = MetricType::Gauge,
          .unit = std::string("ms"),
          .description = std::string("planning-stage budget ceiling"),
      },
      .value = 300.0,
      .ts_unix_ms = 1712563200200,
      .labels = MetricLabels{
          .module = std::string("runtime"),
          .stage = std::string(dasall::infra::metrics::kPlanningMetricStageLabel),
          .profile = std::string("edge_balanced"),
          .outcome = std::string("degraded"),
          .error_code = std::string(metrics_error_code_name(MetricsErrorCode::QueueFull)),
      },
  };

  const MetricSample missing_error_code_sample{
      .identity_ref = degraded_sample.identity_ref,
      .value = degraded_sample.value,
      .ts_unix_ms = degraded_sample.ts_unix_ms + 1,
      .labels = MetricLabels{
          .module = degraded_sample.labels.module,
          .stage = degraded_sample.labels.stage,
          .profile = degraded_sample.labels.profile,
          .outcome = degraded_sample.labels.outcome,
          .error_code = std::string(),
      },
  };

  assert_true(is_planning_observation_sample(degraded_sample),
              "degraded planning-stage metrics should keep an explicit metrics error code so fallback statistics stay observable");
  assert_true(!is_planning_observation_sample(missing_error_code_sample),
              "degraded planning-stage metrics should be rejected when the error_code label is omitted");
}

}  // namespace

int main() {
  try {
    test_planning_stage_metrics_remain_trackable_at_contract_boundary();
    test_planning_stage_degraded_path_requires_explicit_error_code();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}