#include <exception>
#include <iostream>
#include <string>

#include "metrics/MetricTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_metric_identity_and_sample_accept_frozen_valid_defaults() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  const MetricIdentity identity{
      .name = std::string("metrics.samples_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("number of accepted samples"),
  };

  const MetricLabels labels{
      .module = std::string("infra"),
      .stage = std::string("record"),
      .profile = std::string("desktop_full"),
      .outcome = std::string("accepted"),
      .error_code = std::string(),
  };

  const MetricSample sample{
      .identity_ref = identity,
      .value = 1.0,
      .ts_unix_ms = 1711958400000,
      .labels = labels,
  };

  assert_true(identity.is_valid(),
              "MetricIdentity should accept a stable ASCII name and non-empty unit placeholder");
  assert_true(labels.is_valid(),
              "MetricLabels should accept the frozen module/stage/profile/outcome quartet");
  assert_true(sample.is_valid(),
              "MetricSample should accept a non-negative counter value with explicit timestamp and labels");
}

void test_metric_identity_and_labels_reject_invalid_inputs() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricType;
  using dasall::infra::metrics::is_valid_metric_name;
  using dasall::infra::metrics::is_valid_metric_unit;
  using dasall::tests::support::assert_true;

  assert_true(!is_valid_metric_name("9metrics.invalid"),
              "metric names should reject a non-alphabetic leading character");
  assert_true(!is_valid_metric_name("metrics invalid"),
              "metric names should reject spaces outside the OTel ASCII allowlist");
  assert_true(!is_valid_metric_unit(std::string(64, 'x')),
              "metric units should reject placeholder values longer than 63 characters");

  const MetricIdentity invalid_identity{
      .name = std::string("metrics invalid"),
      .type = MetricType::Counter,
      .unit = std::string(),
      .description = std::string("broken"),
  };

  const MetricLabels invalid_labels{
      .module = std::string(),
      .stage = std::string("record"),
      .profile = std::string("desktop_full"),
      .outcome = std::string("accepted"),
      .error_code = std::string(),
  };

  assert_true(!invalid_identity.is_valid(),
              "MetricIdentity should reject invalid names or empty unit placeholders");
  assert_true(!invalid_labels.is_valid(),
              "MetricLabels should reject missing module/stage/profile/outcome placeholders");
}

void test_metric_sample_and_histogram_config_keep_binary_guards() {
  using dasall::infra::metrics::HistogramConfig;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricTemporality;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  const MetricSample invalid_counter_sample{
      .identity_ref = MetricIdentity{
          .name = std::string("metrics.queue_depth"),
          .type = MetricType::Counter,
          .unit = std::string("1"),
          .description = std::string("queue depth counter"),
      },
      .value = -1.0,
      .ts_unix_ms = 1711958400000,
      .labels = MetricLabels{
          .module = std::string("infra"),
          .stage = std::string("record"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("accepted"),
          .error_code = std::string(),
      },
  };

  const HistogramConfig valid_histogram{};
  const HistogramConfig invalid_histogram{
      .buckets = {0.1, 0.05, 0.2},
      .temporality = MetricTemporality::Cumulative,
      .max_scale = 20,
  };

  assert_true(!invalid_counter_sample.is_valid(),
              "counter samples should reject negative values while the counter contract is frozen");
  assert_true(valid_histogram.is_valid(),
              "HistogramConfig should accept the frozen default monotonic bucket set");
  assert_true(!invalid_histogram.is_valid(),
              "HistogramConfig should reject non-monotonic bucket boundaries");
}

}  // namespace

int main() {
  try {
    test_metric_identity_and_sample_accept_frozen_valid_defaults();
    test_metric_identity_and_labels_reject_invalid_inputs();
    test_metric_sample_and_histogram_config_keep_binary_guards();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}