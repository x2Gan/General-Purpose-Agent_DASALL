#include "metrics/MetricsConfigPolicy.h"

#include <algorithm>
#include <string>
#include <utility>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricsConfigPolicySourceRef = "MetricsConfigPolicy";

[[nodiscard]] MetricPolicyResult make_policy_failure(MetricsErrorCode code,
                                                     std::string message,
                                                     std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricPolicyResult::reject(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kMetricsConfigPolicySourceRef) + ":" +
          std::string(metrics_error_code_name(code)));
}

[[nodiscard]] MetricLabelsNormalizationResult make_normalization_failure(
    MetricsErrorCode code,
    std::string message,
    std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricLabelsNormalizationResult::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kMetricsConfigPolicySourceRef) + ":" +
          std::string(metrics_error_code_name(code)));
}

[[nodiscard]] bool is_valid_allowlist(const std::vector<std::string>& allowlist) {
  if (allowlist.size() != kMetricLabelAllowlist.size()) {
    return false;
  }

  for (const auto key : kMetricLabelAllowlist) {
    if (std::find(allowlist.begin(), allowlist.end(), std::string(key)) == allowlist.end()) {
      return false;
    }
  }

  for (std::size_t index = 0; index < allowlist.size(); ++index) {
    if (allowlist[index].empty()) {
      return false;
    }

    if (std::find(allowlist.begin() + static_cast<std::ptrdiff_t>(index + 1),
                  allowlist.end(),
                  allowlist[index]) != allowlist.end()) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool MetricsResolvedConfig::is_valid() const {
  return enabled && !provider_type.empty() && !exporter_type.empty() &&
         reader_interval_ms > 0 && exporter_timeout_ms > 0 &&
         is_valid_allowlist(label_allowlist) && histogram_config.is_valid();
}

bool MetricsResolvedConfig::has_allowlist_key(std::string_view key) const {
  return std::find(label_allowlist.begin(), label_allowlist.end(), std::string(key)) !=
         label_allowlist.end();
}

MetricsConfigPolicy::MetricsConfigPolicy()
    : default_config_(MetricsResolvedConfig{
          .enabled = true,
          .provider_type = "internal",
          .exporter_type = "noop",
          .reader_interval_ms = 5000,
          .exporter_timeout_ms = 30000,
          .label_allowlist = default_allowlist(),
          .histogram_config = HistogramConfig{},
          .audit_on_policy_change = true,
      }) {}

MetricsConfigPolicy::MetricsConfigPolicy(MetricsResolvedConfig default_config)
    : default_config_(std::move(default_config)) {
  if (!default_config_.is_valid()) {
    default_config_ = MetricsConfigPolicy{}.default_config();
  }
}

MetricPolicyResult MetricsConfigPolicy::validate_identity(const MetricIdentity& identity) const {
  if (!identity.is_valid()) {
    return make_policy_failure(
        MetricsErrorCode::IdentityInvalid,
        "metrics config policy requires a valid metric identity before merge-time acceptance checks",
        "metrics.config.validate_identity");
  }

  return MetricPolicyResult::accept();
}

MetricLabelsNormalizationResult MetricsConfigPolicy::normalize_labels(
    const MetricLabels& labels) const {
  if (!labels.is_valid()) {
    return make_normalization_failure(
        MetricsErrorCode::ConfigInvalid,
        "metrics config policy requires module/stage/profile/outcome labels before normalization",
        "metrics.config.normalize_labels");
  }

  auto normalized = labels;
  if (normalized.error_code.empty()) {
    normalized.error_code = "none";
  }
  if (normalized.decision_kind.empty()) {
    normalized.decision_kind = "none";
  }

  return MetricLabelsNormalizationResult::success(std::move(normalized));
}

MetricPolicyResult MetricsConfigPolicy::should_accept(const MetricLabels& labels) const {
  if (!labels.is_valid()) {
    return make_policy_failure(
        MetricsErrorCode::LabelCardinalityExceeded,
        "metrics config policy rejects incomplete labels outside the frozen allowlist",
        "metrics.config.should_accept");
  }

  if (!default_config_.has_allowlist_key("module") || !default_config_.has_allowlist_key("stage") ||
      !default_config_.has_allowlist_key("profile") || !default_config_.has_allowlist_key("outcome") ||
      !default_config_.has_allowlist_key("error_code")) {
    return make_policy_failure(
        MetricsErrorCode::ConfigInvalid,
        "metrics config policy requires the frozen allowlist set module/stage/profile/outcome/error_code",
        "metrics.config.should_accept");
  }

  return MetricPolicyResult::accept();
}

MetricsResolvedConfig MetricsConfigPolicy::merge(const MetricsConfigPatch& profile,
                                                 const MetricsConfigPatch& deploy,
                                                 const MetricsConfigPatch& runtime) const {
  auto resolved = default_config_;
  apply_patch(resolved, profile);
  apply_patch(resolved, deploy);
  apply_patch(resolved, runtime);

  if (!resolved.is_valid()) {
    return default_config_;
  }

  return resolved;
}

MetricPolicyResult MetricsConfigPolicy::validate_histogram_buckets(
    const std::vector<double>& buckets) const {
  auto candidate = default_config_.histogram_config;
  candidate.buckets = buckets;
  if (!candidate.is_valid()) {
    return make_policy_failure(
        MetricsErrorCode::ConfigInvalid,
        "metrics config policy requires histogram buckets to remain positive and strictly increasing",
        "metrics.config.validate_histogram_buckets");
  }

  return MetricPolicyResult::accept();
}

const MetricsResolvedConfig& MetricsConfigPolicy::default_config() const {
  return default_config_;
}

void MetricsConfigPolicy::apply_patch(MetricsResolvedConfig& resolved,
                                      const MetricsConfigPatch& patch) {
  if (patch.enabled.has_value()) {
    resolved.enabled = *patch.enabled;
  }

  if (patch.provider_type.has_value() && !patch.provider_type->empty()) {
    resolved.provider_type = *patch.provider_type;
  }

  if (patch.exporter_type.has_value() && !patch.exporter_type->empty()) {
    resolved.exporter_type = *patch.exporter_type;
  }

  if (patch.reader_interval_ms.has_value() && *patch.reader_interval_ms > 0U) {
    resolved.reader_interval_ms = *patch.reader_interval_ms;
  }

  if (patch.exporter_timeout_ms.has_value() && *patch.exporter_timeout_ms > 0U) {
    resolved.exporter_timeout_ms = *patch.exporter_timeout_ms;
  }

  if (patch.label_allowlist.has_value() && is_valid_allowlist(*patch.label_allowlist)) {
    resolved.label_allowlist = *patch.label_allowlist;
  }

  if (patch.histogram_buckets.has_value()) {
    auto candidate = resolved.histogram_config;
    candidate.buckets = *patch.histogram_buckets;
    if (candidate.is_valid()) {
      resolved.histogram_config = std::move(candidate);
    }
  }

  if (patch.audit_on_policy_change.has_value()) {
    resolved.audit_on_policy_change = *patch.audit_on_policy_change;
  }
}

std::vector<std::string> MetricsConfigPolicy::default_allowlist() {
  std::vector<std::string> allowlist;
  allowlist.reserve(kMetricLabelAllowlist.size());
  for (const auto key : kMetricLabelAllowlist) {
    allowlist.emplace_back(key);
  }

  return allowlist;
}

}  // namespace dasall::infra::metrics