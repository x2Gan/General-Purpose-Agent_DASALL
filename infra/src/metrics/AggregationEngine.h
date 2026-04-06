#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "metrics/IMetricsProvider.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::metrics {

struct AggregatedMetricValue {
  MetricIdentity identity;
  std::uint64_t sample_count = 0;
  double accumulated_value = 0.0;
  double last_value = 0.0;
  double min_value = 0.0;
  double max_value = 0.0;
  std::vector<std::uint64_t> bucket_counts;

  [[nodiscard]] bool is_valid() const {
    return identity.is_valid() && sample_count > 0;
  }
};

struct AggregationSnapshot {
  std::map<std::string, AggregatedMetricValue> metrics;

  [[nodiscard]] bool empty() const {
    return metrics.empty();
  }

  [[nodiscard]] const AggregatedMetricValue* find(std::string_view metric_name) const {
    const auto existing = metrics.find(std::string(metric_name));
    if (existing == metrics.end()) {
      return nullptr;
    }

    return &existing->second;
  }
};

class AggregationEngine {
 public:
  explicit AggregationEngine(HistogramConfig histogram_config = {});

  MetricsOperationStatus aggregate_counter(const MetricSample& sample);
  MetricsOperationStatus aggregate_gauge(const MetricSample& sample);
  MetricsOperationStatus aggregate_histogram(const MetricSample& sample);
  MetricsOperationStatus aggregate(const MetricSample& sample);
  [[nodiscard]] AggregationSnapshot snapshot() const;

 private:
  using AggregateMap = std::map<std::string, AggregatedMetricValue>;

  MetricsOperationStatus aggregate_updown_counter(const MetricSample& sample);
  [[nodiscard]] MetricsOperationStatus validate_sample(const MetricSample& sample,
                                                       MetricType expected_type,
                                                       std::string_view stage) const;
  AggregatedMetricValue& get_or_create(const MetricSample& sample);
  void update_common_statistics(AggregatedMetricValue& aggregate, double value);
  void update_histogram_bucket(AggregatedMetricValue& aggregate, double value);

  HistogramConfig histogram_config_;
  AggregateMap aggregates_;
};

}  // namespace dasall::infra::metrics