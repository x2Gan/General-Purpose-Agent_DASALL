#include "metrics/AggregationEngine.h"

#include <string>
#include <string_view>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kAggregationEngineSourceRef = "AggregationEngine";

[[nodiscard]] MetricsOperationStatus make_aggregation_failure(
    MetricsErrorCode code,
    std::string message,
    std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricsOperationStatus::failure(mapping.result_code,
                                         std::move(message),
                                         std::move(stage),
                                         std::string(kAggregationEngineSourceRef) + ":" +
                                             std::string(metrics_error_code_name(code)));
}

[[nodiscard]] bool identities_are_equivalent(const MetricIdentity& lhs,
                                             const MetricIdentity& rhs) {
  return lhs.name == rhs.name && lhs.type == rhs.type && lhs.unit == rhs.unit &&
         lhs.description == rhs.description;
}

}  // namespace

AggregationEngine::AggregationEngine(HistogramConfig histogram_config)
    : histogram_config_(histogram_config.is_valid() ? std::move(histogram_config)
                                                    : HistogramConfig{}) {}

MetricsOperationStatus AggregationEngine::aggregate_counter(const MetricSample& sample) {
  const auto validation = validate_sample(sample, MetricType::Counter, "metrics.aggregate.counter");
  if (!validation.ok) {
    return validation;
  }

  auto& aggregate = get_or_create(sample);
  update_common_statistics(aggregate, sample.value);
  aggregate.accumulated_value += sample.value;
  aggregate.last_value = aggregate.accumulated_value;
  return MetricsOperationStatus::success("aggregation-engine://counter");
}

MetricsOperationStatus AggregationEngine::aggregate_gauge(const MetricSample& sample) {
  const auto validation = validate_sample(sample, MetricType::Gauge, "metrics.aggregate.gauge");
  if (!validation.ok) {
    return validation;
  }

  auto& aggregate = get_or_create(sample);
  update_common_statistics(aggregate, sample.value);
  aggregate.accumulated_value = sample.value;
  aggregate.last_value = sample.value;
  return MetricsOperationStatus::success("aggregation-engine://gauge");
}

MetricsOperationStatus AggregationEngine::aggregate_histogram(const MetricSample& sample) {
  const auto validation = validate_sample(sample,
                                          MetricType::Histogram,
                                          "metrics.aggregate.histogram");
  if (!validation.ok) {
    return validation;
  }

  auto& aggregate = get_or_create(sample);
  update_common_statistics(aggregate, sample.value);
  aggregate.accumulated_value += sample.value;
  aggregate.last_value = sample.value;
  update_histogram_bucket(aggregate, sample.value);
  return MetricsOperationStatus::success("aggregation-engine://histogram");
}

MetricsOperationStatus AggregationEngine::aggregate(const MetricSample& sample) {
  switch (sample.identity_ref.type) {
    case MetricType::Counter:
      return aggregate_counter(sample);
    case MetricType::Gauge:
      return aggregate_gauge(sample);
    case MetricType::Histogram:
      return aggregate_histogram(sample);
    case MetricType::UpDownCounter:
      return aggregate_updown_counter(sample);
  }

  return make_aggregation_failure(MetricsErrorCode::IdentityInvalid,
                                  "aggregation engine received an unsupported metric type",
                                  "metrics.aggregate");
}

AggregationSnapshot AggregationEngine::snapshot() const {
  return AggregationSnapshot{
      .metrics = aggregates_,
  };
}

MetricsOperationStatus AggregationEngine::aggregate_updown_counter(
    const MetricSample& sample) {
  const auto validation = validate_sample(sample,
                                          MetricType::UpDownCounter,
                                          "metrics.aggregate.updown_counter");
  if (!validation.ok) {
    return validation;
  }

  auto& aggregate = get_or_create(sample);
  update_common_statistics(aggregate, sample.value);
  aggregate.accumulated_value += sample.value;
  aggregate.last_value = aggregate.accumulated_value;
  return MetricsOperationStatus::success("aggregation-engine://updown-counter");
}

MetricsOperationStatus AggregationEngine::validate_sample(const MetricSample& sample,
                                                          MetricType expected_type,
                                                          std::string_view stage) const {
  if (!sample.is_valid() || sample.identity_ref.type != expected_type) {
    return make_aggregation_failure(
        MetricsErrorCode::IdentityInvalid,
        "aggregation engine requires a valid metric sample matching the requested aggregate type",
        std::string(stage));
  }

  const auto existing = aggregates_.find(sample.identity_ref.name);
  if (existing != aggregates_.end() &&
      !identities_are_equivalent(existing->second.identity, sample.identity_ref)) {
    return make_aggregation_failure(
        MetricsErrorCode::IdentityInvalid,
        "aggregation engine rejects same-name samples when metric identity semantics diverge",
        std::string(stage));
  }

  return MetricsOperationStatus::success("aggregation-engine://validated");
}

AggregatedMetricValue& AggregationEngine::get_or_create(const MetricSample& sample) {
  const auto [existing, inserted] = aggregates_.try_emplace(
      sample.identity_ref.name,
      AggregatedMetricValue{
          .identity = sample.identity_ref,
          .sample_count = 0,
          .accumulated_value = 0.0,
          .last_value = 0.0,
          .min_value = 0.0,
          .max_value = 0.0,
          .bucket_counts = sample.identity_ref.type == MetricType::Histogram
                               ? std::vector<std::uint64_t>(histogram_config_.buckets.size() + 1U, 0U)
                               : std::vector<std::uint64_t>{},
      });

  if (inserted && existing->second.identity.type == MetricType::Histogram &&
      existing->second.bucket_counts.empty()) {
    existing->second.bucket_counts.resize(histogram_config_.buckets.size() + 1U, 0U);
  }

  return existing->second;
}

void AggregationEngine::update_common_statistics(AggregatedMetricValue& aggregate,
                                                 double value) {
  if (aggregate.sample_count == 0U) {
    aggregate.min_value = value;
    aggregate.max_value = value;
  } else {
    if (value < aggregate.min_value) {
      aggregate.min_value = value;
    }
    if (value > aggregate.max_value) {
      aggregate.max_value = value;
    }
  }

  ++aggregate.sample_count;
}

void AggregationEngine::update_histogram_bucket(AggregatedMetricValue& aggregate,
                                                double value) {
  if (aggregate.bucket_counts.empty()) {
    aggregate.bucket_counts.resize(histogram_config_.buckets.size() + 1U, 0U);
  }

  for (std::size_t index = 0; index < histogram_config_.buckets.size(); ++index) {
    if (value <= histogram_config_.buckets[index]) {
      ++aggregate.bucket_counts[index];
      return;
    }
  }

  ++aggregate.bucket_counts.back();
}

}  // namespace dasall::infra::metrics