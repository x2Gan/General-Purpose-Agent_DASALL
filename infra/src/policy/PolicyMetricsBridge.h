#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricTypes.h"
#include "policy/PolicyErrors.h"

namespace dasall::infra::policy {

enum class PolicyMetricKind {
  ReloadTotal = 0,
  InvalidTotal,
  PatchTotal,
  DenyTotal,
  RollbackTotal,
  ActiveGeneration,
  SafeModeTotal,
};

inline constexpr std::string_view kPolicyMetricsMeterScopeName = "infra.policy";
inline constexpr std::string_view kPolicyMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kPolicyMetricModuleLabel = "policy";
inline constexpr std::string_view kPolicyMetricNoErrorCodeLabel = "none";
inline constexpr std::array<std::string_view, 6> kPolicyMetricAllowedStages{
    "load",
    "validate",
    "apply_patch",
    "query",
    "rollback",
    "safe_mode",
};
inline constexpr std::array<std::string_view, 4> kPolicyMetricAllowedOutcomes{
    "success",
    "failure",
    "rejected",
    "degraded",
};
inline constexpr std::size_t kPolicyMetricFamilyCount = 7U;

[[nodiscard]] inline constexpr std::string_view policy_metric_name(
    PolicyMetricKind kind) {
  switch (kind) {
    case PolicyMetricKind::ReloadTotal:
      return "infra_policy_reload_total";
    case PolicyMetricKind::InvalidTotal:
      return "infra_policy_invalid_total";
    case PolicyMetricKind::PatchTotal:
      return "infra_policy_patch_total";
    case PolicyMetricKind::DenyTotal:
      return "infra_policy_deny_total";
    case PolicyMetricKind::RollbackTotal:
      return "infra_policy_rollback_total";
    case PolicyMetricKind::ActiveGeneration:
      return "infra_policy_active_generation";
    case PolicyMetricKind::SafeModeTotal:
      return "infra_policy_safe_mode_total";
  }

  return "infra_policy_unknown_metric";
}

[[nodiscard]] inline constexpr metrics::MetricType policy_metric_type(
    PolicyMetricKind kind) {
  switch (kind) {
    case PolicyMetricKind::ActiveGeneration:
      return metrics::MetricType::Gauge;
    case PolicyMetricKind::ReloadTotal:
    case PolicyMetricKind::InvalidTotal:
    case PolicyMetricKind::PatchTotal:
    case PolicyMetricKind::DenyTotal:
    case PolicyMetricKind::RollbackTotal:
    case PolicyMetricKind::SafeModeTotal:
      return metrics::MetricType::Counter;
  }

  return metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view policy_metric_unit(
    PolicyMetricKind) {
  return "1";
}

[[nodiscard]] inline bool is_policy_metric_stage(std::string_view stage) {
  return std::find(kPolicyMetricAllowedStages.begin(),
                   kPolicyMetricAllowedStages.end(),
                   stage) != kPolicyMetricAllowedStages.end();
}

[[nodiscard]] inline bool is_policy_metric_outcome(std::string_view outcome) {
  return std::find(kPolicyMetricAllowedOutcomes.begin(),
                   kPolicyMetricAllowedOutcomes.end(),
                   outcome) != kPolicyMetricAllowedOutcomes.end();
}

[[nodiscard]] inline bool is_policy_metric_error_code(
  const std::string_view& error_code) {
  return error_code == kPolicyMetricNoErrorCodeLabel ||
         error_code == policy_error_code_name(PolicyErrorCode::BundleInvalid) ||
         error_code == policy_error_code_name(PolicyErrorCode::SchemaUnsupported) ||
         error_code == policy_error_code_name(PolicyErrorCode::ConflictUnresolved) ||
         error_code == policy_error_code_name(PolicyErrorCode::PatchBaseMismatch) ||
         error_code == policy_error_code_name(PolicyErrorCode::SnapshotNotFound) ||
         error_code == policy_error_code_name(PolicyErrorCode::RollbackFailed) ||
         error_code == policy_error_code_name(PolicyErrorCode::QueryDenied) ||
         error_code == policy_error_code_name(PolicyErrorCode::SourceUnavailable) ||
         error_code == policy_error_code_name(PolicyErrorCode::StoreCommitFailed) ||
         error_code == policy_error_code_name(PolicyErrorCode::DryRunRejected);
}

[[nodiscard]] metrics::MetricIdentity make_policy_metric_identity(
    PolicyMetricKind kind);

struct PolicyMetricSignal {
  PolicyMetricKind kind = PolicyMetricKind::ReloadTotal;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage = std::string(kPolicyMetricAllowedStages.front());
  std::string outcome = std::string(kPolicyMetricAllowedOutcomes.front());
  std::optional<PolicyErrorCode> policy_error_code;

  [[nodiscard]] bool has_consistent_values() const {
    if (!std::isfinite(value) || value < 0.0 || ts_unix_ms <= 0 ||
        !is_policy_metric_stage(stage) || !is_policy_metric_outcome(outcome)) {
      return false;
    }

    const bool has_error_code = policy_error_code.has_value();
    if (outcome == "success" && has_error_code) {
      return false;
    }

    switch (kind) {
      case PolicyMetricKind::ReloadTotal:
        return stage == "load" &&
               ((outcome == "success" && !has_error_code) ||
                (outcome == "failure" && has_error_code));
      case PolicyMetricKind::InvalidTotal:
        return (stage == "validate" || stage == "load" || stage == "apply_patch") &&
               (outcome == "failure" || outcome == "rejected") && has_error_code;
      case PolicyMetricKind::PatchTotal:
        return stage == "apply_patch" &&
               ((outcome == "success" && !has_error_code) ||
                ((outcome == "failure" || outcome == "rejected" || outcome == "degraded") &&
                 has_error_code));
      case PolicyMetricKind::DenyTotal:
        return stage == "query" && outcome == "rejected" && has_error_code;
      case PolicyMetricKind::RollbackTotal:
        return stage == "rollback" &&
               ((outcome == "success" && !has_error_code) ||
                ((outcome == "failure" || outcome == "degraded") && has_error_code));
      case PolicyMetricKind::ActiveGeneration:
        return (stage == "load" || stage == "apply_patch" || stage == "rollback" ||
                stage == "safe_mode") &&
               (outcome == "success" || outcome == "degraded") && !has_error_code;
      case PolicyMetricKind::SafeModeTotal:
        return stage == "safe_mode" && outcome == "degraded" && !has_error_code;
    }

    return false;
  }
};

struct PolicyMetricsEmitResult {
  bool emitted = false;
  bool bridge_degraded = false;
  metrics::MetricsOperationStatus status =
      metrics::MetricsOperationStatus::success();
  std::optional<metrics::MetricsErrorCode> metrics_error_code;

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return status.ok && !metrics_error_code.has_value();
    }

    return !status.ok && metrics_error_code.has_value();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    return status.references_only_contract_error_types();
  }
};

class PolicyMetricsBridge {
 public:
  explicit PolicyMetricsBridge(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");

  PolicyMetricsEmitResult emit(const PolicyMetricSignal& signal);

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] bool has_active_meter() const {
    return static_cast<bool>(meter_);
  }

  [[nodiscard]] bool instruments_registered() const {
    return instruments_registered_;
  }

  [[nodiscard]] const std::string& profile_id() const {
    return profile_id_;
  }

  [[nodiscard]] std::uint64_t emission_attempt_total() const {
    return emission_attempt_total_;
  }

  [[nodiscard]] std::uint64_t emission_failure_total() const {
    return emission_failure_total_;
  }

  [[nodiscard]] std::optional<metrics::MetricsErrorCode> last_metrics_error_code()
      const {
    return last_metrics_error_code_;
  }

 private:
  static constexpr std::size_t to_index(PolicyMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  bool ensure_meter_ready(PolicyMetricsEmitResult* failure);
  bool ensure_instruments_registered(PolicyMetricsEmitResult* failure);
  PolicyMetricsEmitResult make_failure_result(
      metrics::MetricsErrorCode error_code,
      metrics::MetricsOperationStatus status);
  metrics::MetricSample make_sample(const PolicyMetricSignal& signal) const;

  std::shared_ptr<metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<metrics::IMeter> meter_;
  std::string profile_id_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  std::array<std::optional<metrics::InstrumentHandle>,
             kPolicyMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t emission_attempt_total_ = 0;
  std::uint64_t emission_failure_total_ = 0;
  std::optional<metrics::MetricsErrorCode> last_metrics_error_code_;
};

}  // namespace dasall::infra::policy