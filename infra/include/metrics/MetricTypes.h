#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::infra::metrics {

enum class MetricType {
  Counter,
  Gauge,
  Histogram,
  UpDownCounter,
};

enum class MetricTemporality {
  Cumulative,
  Delta,
};

inline constexpr std::array<std::string_view, 5> kMetricLabelAllowlist{
    "module",
    "stage",
    "profile",
    "outcome",
    "error_code",
};

[[nodiscard]] inline bool is_valid_metric_name(std::string_view name) {
  if (name.empty() || !std::isalpha(static_cast<unsigned char>(name.front()))) {
    return false;
  }

  if (name.size() > 255) {
    return false;
  }

  for (const char ch : name) {
    const auto code = static_cast<unsigned char>(ch);
    if (std::isalnum(code) || ch == '_' || ch == '.' || ch == '-' || ch == '/') {
      continue;
    }

    return false;
  }

  return true;
}

[[nodiscard]] inline bool is_valid_metric_unit(std::string_view unit) {
  if (unit.empty() || unit.size() > 63) {
    return false;
  }

  for (const char ch : unit) {
    const auto code = static_cast<unsigned char>(ch);
    if (code < 33 || code > 126) {
      return false;
    }
  }

  return true;
}

struct MetricIdentity {
  std::string name;
  MetricType type = MetricType::Counter;
  std::string unit = "1";
  std::string description;

  [[nodiscard]] bool is_valid() const {
    return is_valid_metric_name(name) && is_valid_metric_unit(unit);
  }
};

struct MetricLabels {
  std::string module;
  std::string stage;
  std::string profile;
  std::string outcome;
  std::string error_code;

  [[nodiscard]] bool is_valid() const {
    return !module.empty() && !stage.empty() && !profile.empty() && !outcome.empty();
  }
};

struct MetricSample {
  MetricIdentity identity_ref;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  MetricLabels labels;

  [[nodiscard]] bool is_valid() const {
    if (!identity_ref.is_valid() || !labels.is_valid() || ts_unix_ms <= 0 || !std::isfinite(value)) {
      return false;
    }

    switch (identity_ref.type) {
      case MetricType::Counter:
      case MetricType::Histogram:
        return value >= 0.0;
      case MetricType::Gauge:
      case MetricType::UpDownCounter:
        return true;
    }

    return false;
  }
};

struct HistogramConfig {
  std::vector<double> buckets{0.005, 0.01, 0.025, 0.05, 0.1, 0.2, 0.3, 0.5, 1.0, 2.0, 5.0};
  MetricTemporality temporality = MetricTemporality::Cumulative;
  std::uint32_t max_scale = 20;

  [[nodiscard]] bool is_valid() const {
    if (buckets.empty()) {
      return false;
    }

    double previous = 0.0;
    bool first = true;
    for (const double bucket : buckets) {
      if (!std::isfinite(bucket) || bucket <= 0.0) {
        return false;
      }

      if (!first && bucket <= previous) {
        return false;
      }

      first = false;
      previous = bucket;
    }

    return max_scale > 0;
  }
};

}  // namespace dasall::infra::metrics