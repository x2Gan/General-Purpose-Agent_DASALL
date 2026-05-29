#include "metrics/CardinalityGuard.h"

#include <algorithm>
#include <utility>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kCardinalityGuardSourceRef = "CardinalityGuard";

[[nodiscard]] CardinalityGuardDecision make_guard_rejection(
    std::string_view metric_name,
    std::string_view label_key,
    std::string message,
    std::string stage) {
  const auto mapping = map_metrics_error_code(MetricsErrorCode::LabelCardinalityExceeded);
  const std::string scoped_message = std::move(message) + " [metric=" +
                                     std::string(metric_name) + ", label=" +
                                     std::string(label_key) + "]";
  return CardinalityGuardDecision::reject(
      mapping.result_code,
      scoped_message,
      std::move(stage),
      std::string(kCardinalityGuardSourceRef) + ":" +
          std::string(metrics_error_code_name(MetricsErrorCode::LabelCardinalityExceeded)),
      scoped_message);
}

}  // namespace

CardinalityGuard::CardinalityGuard(std::size_t max_cardinality_per_metric)
    : max_cardinality_per_metric_(max_cardinality_per_metric > 0U
                                      ? max_cardinality_per_metric
                                      : 200U) {}

CardinalityGuardDecision CardinalityGuard::validate_labels(std::string_view metric_name,
                                                           const MetricLabels& labels) {
  return validate_labels(metric_name, to_entries(normalize_labels(labels)));
}

CardinalityGuardDecision CardinalityGuard::validate_labels(
    std::string_view metric_name,
    const std::vector<MetricLabelEntry>& labels) {
  if (metric_name.empty()) {
    return reject_with_reason(metric_name,
                              "metric_name",
                              "cardinality guard requires a non-empty metric name");
  }

  for (const auto& label : labels) {
    if (!is_allowlisted(label.key)) {
      return reject_with_reason(metric_name,
                                label.key,
                                "cardinality guard rejects labels outside the frozen allowlist");
    }
  }

  const auto materialized = materialize_labels(labels);
  if (!materialized.has_value()) {
    return reject_with_reason(
        metric_name,
        "labels",
        "cardinality guard requires module/stage/profile/outcome labels exactly once and only allowlisted keys");
  }

  const auto signature = make_series_signature(*materialized);
  auto& observed = observed_series_[std::string(metric_name)];
  if (observed.find(signature) == observed.end() &&
      observed.size() >= max_cardinality_per_metric_) {
    return reject_with_reason(
        metric_name,
        "labels",
        "cardinality guard rejected a new label set because the per-metric cardinality limit was exceeded");
  }

  observed.insert(signature);
  return CardinalityGuardDecision::accept(*materialized);
}

CardinalityGuardDecision CardinalityGuard::reject_with_reason(
    std::string_view metric_name,
    std::string_view label_key,
    std::string message,
    std::string stage) {
  increment_reject_total();
  return make_guard_rejection(metric_name, label_key, std::move(message), std::move(stage));
}

std::uint64_t CardinalityGuard::reject_total() const {
  return reject_total_;
}

std::size_t CardinalityGuard::observed_series_count(std::string_view metric_name) const {
  const auto existing = observed_series_.find(std::string(metric_name));
  if (existing == observed_series_.end()) {
    return 0U;
  }

  return existing->second.size();
}

std::size_t CardinalityGuard::max_cardinality_per_metric() const {
  return max_cardinality_per_metric_;
}

MetricLabels CardinalityGuard::normalize_labels(const MetricLabels& labels) {
  auto normalized = labels;
  if (normalized.error_code.empty()) {
    normalized.error_code = "none";
  }
  if (normalized.resolved_route.empty()) {
    normalized.resolved_route = "none";
  }
  if (normalized.failure_category.empty()) {
    normalized.failure_category = "none";
  }
  if (normalized.error_type.empty()) {
    normalized.error_type = "none";
  }

  return normalized;
}

std::vector<MetricLabelEntry> CardinalityGuard::to_entries(const MetricLabels& labels) {
  return std::vector<MetricLabelEntry>{
      MetricLabelEntry{.key = "module", .value = labels.module},
      MetricLabelEntry{.key = "stage", .value = labels.stage},
      MetricLabelEntry{.key = "profile", .value = labels.profile},
      MetricLabelEntry{.key = "outcome", .value = labels.outcome},
      MetricLabelEntry{.key = "error_code", .value = labels.error_code},
      MetricLabelEntry{.key = "resolved_route", .value = labels.resolved_route},
      MetricLabelEntry{.key = "failure_category", .value = labels.failure_category},
      MetricLabelEntry{.key = "error_type", .value = labels.error_type},
  };
}

std::optional<MetricLabels> CardinalityGuard::materialize_labels(
    const std::vector<MetricLabelEntry>& labels) {
  MetricLabels materialized;
  bool has_module = false;
  bool has_stage = false;
  bool has_profile = false;
  bool has_outcome = false;
  bool has_error_code = false;
  bool has_resolved_route = false;
  bool has_failure_category = false;
  bool has_error_type = false;

  for (const auto& label : labels) {
    if (label.key == "module") {
      if (has_module) {
        return std::nullopt;
      }
      materialized.module = label.value;
      has_module = true;
      continue;
    }

    if (label.key == "stage") {
      if (has_stage) {
        return std::nullopt;
      }
      materialized.stage = label.value;
      has_stage = true;
      continue;
    }

    if (label.key == "profile") {
      if (has_profile) {
        return std::nullopt;
      }
      materialized.profile = label.value;
      has_profile = true;
      continue;
    }

    if (label.key == "outcome") {
      if (has_outcome) {
        return std::nullopt;
      }
      materialized.outcome = label.value;
      has_outcome = true;
      continue;
    }

    if (label.key == "error_code") {
      if (has_error_code) {
        return std::nullopt;
      }
      materialized.error_code = label.value;
      has_error_code = true;
      continue;
    }

    if (label.key == "resolved_route") {
      if (has_resolved_route) {
        return std::nullopt;
      }
      materialized.resolved_route = label.value;
      has_resolved_route = true;
      continue;
    }

    if (label.key == "failure_category") {
      if (has_failure_category) {
        return std::nullopt;
      }
      materialized.failure_category = label.value;
      has_failure_category = true;
      continue;
    }

    if (label.key == "error_type") {
      if (has_error_type) {
        return std::nullopt;
      }
      materialized.error_type = label.value;
      has_error_type = true;
      continue;
    }

    return std::nullopt;
  }

  if (!has_error_code || materialized.error_code.empty()) {
    materialized.error_code = "none";
  }
  if (!has_resolved_route || materialized.resolved_route.empty()) {
    materialized.resolved_route = "none";
  }
  if (!has_failure_category || materialized.failure_category.empty()) {
    materialized.failure_category = "none";
  }
  if (!has_error_type || materialized.error_type.empty()) {
    materialized.error_type = "none";
  }

  if (!has_module || !has_stage || !has_profile || !has_outcome || !materialized.is_valid()) {
    return std::nullopt;
  }

  return materialized;
}

bool CardinalityGuard::is_allowlisted(std::string_view key) {
  return std::find(kMetricLabelAllowlist.begin(), kMetricLabelAllowlist.end(), key) !=
         kMetricLabelAllowlist.end();
}

std::string CardinalityGuard::make_series_signature(const MetricLabels& labels) {
  return labels.module + "\x1f" + labels.stage + "\x1f" + labels.profile + "\x1f" +
         labels.outcome + "\x1f" + labels.error_code + "\x1f" +
         labels.resolved_route + "\x1f" + labels.failure_category + "\x1f" +
         labels.error_type;
}

void CardinalityGuard::increment_reject_total() {
  ++reject_total_;
}

}  // namespace dasall::infra::metrics