#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "metrics/MetricTypes.h"

namespace dasall::infra::metrics {

struct MetricLabelEntry {
  std::string key;
  std::string value;
};

struct CardinalityGuardDecision {
  bool accepted = false;
  MetricLabels labels{};
  contracts::ResultCode result_code = contracts::ResultCode::PolicyDenied;
  std::optional<contracts::ErrorInfo> error;
  std::string rejection_reason;

  [[nodiscard]] static CardinalityGuardDecision accept(MetricLabels labels) {
    return CardinalityGuardDecision{
        .accepted = true,
        .labels = std::move(labels),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
        .rejection_reason = {},
    };
  }

  [[nodiscard]] static CardinalityGuardDecision reject(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref,
      std::string rejection_reason) {
    return CardinalityGuardDecision{
        .accepted = false,
        .labels = {},
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.metrics",
                .ref_id = std::move(source_ref),
            },
        },
        .rejection_reason = std::move(rejection_reason),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return accepted;
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class CardinalityGuard {
 public:
  explicit CardinalityGuard(std::size_t max_cardinality_per_metric = 200U);

  [[nodiscard]] CardinalityGuardDecision validate_labels(std::string_view metric_name,
                                                         const MetricLabels& labels);
  [[nodiscard]] CardinalityGuardDecision validate_labels(
      std::string_view metric_name,
      const std::vector<MetricLabelEntry>& labels);
  [[nodiscard]] CardinalityGuardDecision reject_with_reason(
      std::string_view metric_name,
      std::string_view label_key,
      std::string message,
      std::string stage = "metrics.guard.validate");
  [[nodiscard]] std::uint64_t reject_total() const;
  [[nodiscard]] std::size_t observed_series_count(std::string_view metric_name) const;
  [[nodiscard]] std::size_t max_cardinality_per_metric() const;

 private:
  using ObservedSeriesSet = std::set<std::string>;
  using ObservedSeriesMap = std::map<std::string, ObservedSeriesSet>;

  [[nodiscard]] static MetricLabels normalize_labels(const MetricLabels& labels);
  [[nodiscard]] static std::vector<MetricLabelEntry> to_entries(const MetricLabels& labels);
  [[nodiscard]] static std::optional<MetricLabels> materialize_labels(
      const std::vector<MetricLabelEntry>& labels);
  [[nodiscard]] static bool is_allowlisted(std::string_view key);
  [[nodiscard]] static std::string make_series_signature(const MetricLabels& labels);
  void increment_reject_total();

  std::size_t max_cardinality_per_metric_ = 200U;
  std::uint64_t reject_total_ = 0;
  ObservedSeriesMap observed_series_;
};

}  // namespace dasall::infra::metrics