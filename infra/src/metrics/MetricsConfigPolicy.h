#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "metrics/IMetricConfigPolicy.h"

namespace dasall::infra::metrics {

struct MetricsConfigPatch {
  std::optional<bool> enabled;
  std::optional<std::string> provider_type;
  std::optional<std::string> exporter_type;
  std::optional<std::uint32_t> reader_interval_ms;
  std::optional<std::uint32_t> exporter_timeout_ms;
  std::optional<std::vector<std::string>> label_allowlist;
  std::optional<std::vector<double>> histogram_buckets;
  std::optional<bool> audit_on_policy_change;
};

struct MetricsResolvedConfig {
  bool enabled = true;
  std::string provider_type = "internal";
  std::string exporter_type = "noop";
  std::uint32_t reader_interval_ms = 5000;
  std::uint32_t exporter_timeout_ms = 30000;
  std::vector<std::string> label_allowlist;
  HistogramConfig histogram_config{};
  bool audit_on_policy_change = true;

  [[nodiscard]] bool is_valid() const;
  [[nodiscard]] bool has_allowlist_key(std::string_view key) const;
};

class MetricsConfigPolicy final : public IMetricConfigPolicy {
 public:
  MetricsConfigPolicy();
  explicit MetricsConfigPolicy(MetricsResolvedConfig default_config);

  [[nodiscard]] MetricPolicyResult validate_identity(const MetricIdentity& identity) const override;
  [[nodiscard]] MetricLabelsNormalizationResult normalize_labels(
      const MetricLabels& labels) const override;
  [[nodiscard]] MetricPolicyResult should_accept(const MetricLabels& labels) const override;

  [[nodiscard]] MetricsResolvedConfig merge(const MetricsConfigPatch& profile,
                                            const MetricsConfigPatch& deploy,
                                            const MetricsConfigPatch& runtime) const;
  [[nodiscard]] MetricPolicyResult validate_histogram_buckets(
      const std::vector<double>& buckets) const;
  [[nodiscard]] const MetricsResolvedConfig& default_config() const;

 private:
  static void apply_patch(MetricsResolvedConfig& resolved, const MetricsConfigPatch& patch);
  static std::vector<std::string> default_allowlist();

  MetricsResolvedConfig default_config_;
};

}  // namespace dasall::infra::metrics