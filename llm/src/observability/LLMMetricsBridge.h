#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "logging/ILogger.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricTypes.h"

namespace dasall::llm::observability {

enum class LLMMetricKind {
  CallsTotal = 0,
  CallLatencyMs,
  FallbackTotal,
  ModelSelectionTotal,
  ReasoningEscalationTotal,
  PromptPolicyDenyTotal,
  PromptComposeOverBudgetTotal,
  AdapterTimeoutTotal,
  HealthDegradedTotal,
  PromptCacheHitTokensTotal,
  PromptCacheMissTokensTotal,
  CostEstimateUsdTotal,
};

inline constexpr std::string_view kLLMMetricsMeterScopeName = "llm.observability";
inline constexpr std::string_view kLLMMetricsMeterScopeVersion = "v1";
inline constexpr std::string_view kLLMMetricModuleLabel = "llm";
inline constexpr std::string_view kLLMMetricNoErrorCodeLabel = "none";
inline constexpr std::array<std::string_view, 4> kLLMMetricAllowedOutcomes{
    "success",
    "degraded",
    "failure",
    "rejected",
};
inline constexpr std::size_t kLLMMetricFamilyCount = 12U;

[[nodiscard]] inline constexpr std::string_view llm_metric_name(LLMMetricKind kind) {
  switch (kind) {
    case LLMMetricKind::CallsTotal:
      return "llm_calls_total";
    case LLMMetricKind::CallLatencyMs:
      return "llm_call_latency_ms";
    case LLMMetricKind::FallbackTotal:
      return "llm_fallback_total";
    case LLMMetricKind::ModelSelectionTotal:
      return "llm_model_selection_total";
    case LLMMetricKind::ReasoningEscalationTotal:
      return "llm_reasoning_escalation_total";
    case LLMMetricKind::PromptPolicyDenyTotal:
      return "prompt_policy_deny_total";
    case LLMMetricKind::PromptComposeOverBudgetTotal:
      return "prompt_compose_over_budget_total";
    case LLMMetricKind::AdapterTimeoutTotal:
      return "llm_adapter_timeout_total";
    case LLMMetricKind::HealthDegradedTotal:
      return "llm_health_degraded_total";
    case LLMMetricKind::PromptCacheHitTokensTotal:
      return "llm_prompt_cache_hit_tokens_total";
    case LLMMetricKind::PromptCacheMissTokensTotal:
      return "llm_prompt_cache_miss_tokens_total";
    case LLMMetricKind::CostEstimateUsdTotal:
      return "llm_cost_estimate_usd_total";
  }

  return "llm_unknown_metric";
}

[[nodiscard]] inline constexpr infra::metrics::MetricType llm_metric_type(
    LLMMetricKind kind) {
  switch (kind) {
    case LLMMetricKind::CallLatencyMs:
      return infra::metrics::MetricType::Histogram;
    case LLMMetricKind::CallsTotal:
    case LLMMetricKind::FallbackTotal:
    case LLMMetricKind::ModelSelectionTotal:
    case LLMMetricKind::ReasoningEscalationTotal:
    case LLMMetricKind::PromptPolicyDenyTotal:
    case LLMMetricKind::PromptComposeOverBudgetTotal:
    case LLMMetricKind::AdapterTimeoutTotal:
    case LLMMetricKind::HealthDegradedTotal:
    case LLMMetricKind::PromptCacheHitTokensTotal:
    case LLMMetricKind::PromptCacheMissTokensTotal:
    case LLMMetricKind::CostEstimateUsdTotal:
      return infra::metrics::MetricType::Counter;
  }

  return infra::metrics::MetricType::Counter;
}

[[nodiscard]] inline constexpr std::string_view llm_metric_unit(LLMMetricKind kind) {
  switch (kind) {
    case LLMMetricKind::CallLatencyMs:
      return "ms";
    case LLMMetricKind::CostEstimateUsdTotal:
      return "usd";
    case LLMMetricKind::CallsTotal:
    case LLMMetricKind::FallbackTotal:
    case LLMMetricKind::ModelSelectionTotal:
    case LLMMetricKind::ReasoningEscalationTotal:
    case LLMMetricKind::PromptPolicyDenyTotal:
    case LLMMetricKind::PromptComposeOverBudgetTotal:
    case LLMMetricKind::AdapterTimeoutTotal:
    case LLMMetricKind::HealthDegradedTotal:
    case LLMMetricKind::PromptCacheHitTokensTotal:
    case LLMMetricKind::PromptCacheMissTokensTotal:
      return "1";
  }

  return "1";
}

[[nodiscard]] inline bool is_llm_metric_outcome(std::string_view outcome) {
  return std::find(kLLMMetricAllowedOutcomes.begin(),
                   kLLMMetricAllowedOutcomes.end(),
                   outcome) != kLLMMetricAllowedOutcomes.end();
}

struct LLMCallSummary {
  std::string request_id;
  std::string llm_call_id;
  std::string stage;
  std::string resolved_route;
  std::string model_name;
  std::string prompt_id;
  std::string prompt_version;
  bool fallback_used = false;
  std::int64_t completed_at_ms = 0;
  std::uint32_t latency_ms = 0;
  std::string failure_category;
  std::string error_type;
  std::vector<std::string> selection_reason_codes;
  std::uint32_t estimated_input_tokens = 0;
  std::uint32_t prompt_cache_hit_tokens = 0;
  std::uint32_t prompt_cache_miss_tokens = 0;
  double actual_cost_estimate_usd = 0.0;
  std::string reasoning_mode_requested;
  std::string reasoning_mode_effective;
  std::string provider_id;
  std::string profile_id = "unknown";
  std::string outcome = "success";
  std::string request_mode = "unary";
  std::string result_code;
  std::string result_code_category;
  std::string error_stage;
  std::string error_message;
  std::string source_ref_type;
  std::string source_ref_id;
  std::vector<std::string> attempted_routes;
  bool retryable = false;
  bool safe_to_replan = false;
  std::string governance_disposition;
  std::string from_route;
  std::string to_route;
  bool prompt_policy_denied = false;
  bool prompt_compose_over_budget = false;
  bool adapter_timeout = false;
  bool health_degraded = false;
  bool reasoning_escalated = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct LLMLoggingWriteResult {
  bool emitted = false;
  infra::logging::LogEvent log_event{};
  infra::logging::LogWriteResult write_result =
      infra::logging::LogWriteResult::success();

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return write_result.ok && log_event.attrs_are_serializable() &&
             log_event.has_timestamp();
    }

    return !write_result.ok;
  }
};

struct LLMMetricsEmitResult {
  bool emitted = false;
  std::size_t emitted_samples = 0U;
  bool bridge_degraded = false;
  infra::metrics::MetricsOperationStatus status =
      infra::metrics::MetricsOperationStatus::success();
  std::optional<infra::metrics::MetricsErrorCode> metrics_error_code;

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return emitted_samples > 0U && status.ok && !metrics_error_code.has_value();
    }

    return !status.ok && metrics_error_code.has_value();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    return status.references_only_contract_error_types();
  }
};

struct LLMObservabilityRecordResult {
  bool emitted = false;
  LLMLoggingWriteResult log_result{};
  LLMMetricsEmitResult metrics_result{};

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return log_result.has_consistent_state() && metrics_result.has_consistent_state();
    }

    return log_result.has_consistent_state() || metrics_result.has_consistent_state();
  }
};

struct LLMMetricsBridgeStatus {
  std::uint64_t log_emitted_total = 0U;
  std::uint64_t log_emit_failures = 0U;
  std::uint64_t metric_emission_attempt_total = 0U;
  std::uint64_t metric_emission_failure_total = 0U;
  bool degraded = false;
  bool has_active_meter = false;
  bool instruments_registered = false;
  std::optional<dasall::contracts::ResultCode> last_log_error_code;
  std::optional<infra::metrics::MetricsErrorCode> last_metrics_error_code;

  [[nodiscard]] bool is_valid() const;
};

class LLMMetricsBridge {
 public:
  explicit LLMMetricsBridge(
      std::shared_ptr<infra::logging::ILogger> logger = nullptr,
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider = nullptr);

  void set_logger(std::shared_ptr<infra::logging::ILogger> logger);
  void set_metrics_provider(
      std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider);

  [[nodiscard]] bool has_logger() const {
    return static_cast<bool>(logger_);
  }

  [[nodiscard]] bool has_active_meter() const {
    return static_cast<bool>(meter_);
  }

  [[nodiscard]] bool instruments_registered() const {
    return instruments_registered_;
  }

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] LLMLoggingWriteResult write_call_log(const LLMCallSummary& summary);
  [[nodiscard]] LLMMetricsEmitResult record_call_metrics(const LLMCallSummary& summary);
  [[nodiscard]] LLMObservabilityRecordResult record_call(const LLMCallSummary& summary);
  [[nodiscard]] LLMMetricsBridgeStatus get_status() const;

 private:
  static constexpr std::size_t to_index(LLMMetricKind kind) {
    return static_cast<std::size_t>(kind);
  }

  bool ensure_meter_ready(LLMMetricsEmitResult* failure);
  bool ensure_instruments_registered(LLMMetricsEmitResult* failure);
  infra::logging::LogEvent make_log_event(const LLMCallSummary& summary) const;
  std::vector<infra::metrics::MetricSample> make_metric_samples(
      const LLMCallSummary& summary) const;
  LLMMetricsEmitResult make_metrics_failure_result(
      infra::metrics::MetricsErrorCode error_code,
      infra::metrics::MetricsOperationStatus status);
  void record_log_success();
  void record_log_failure(std::optional<dasall::contracts::ResultCode> result_code);

  std::shared_ptr<infra::logging::ILogger> logger_;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_;
  std::shared_ptr<infra::metrics::IMeter> meter_;
  bool degraded_ = false;
  bool instruments_registered_ = false;
  std::array<std::optional<infra::metrics::InstrumentHandle>, kLLMMetricFamilyCount>
      instrument_handles_{};
  std::uint64_t log_emitted_total_ = 0U;
  std::uint64_t log_emit_failures_ = 0U;
  std::uint64_t metric_emission_attempt_total_ = 0U;
  std::uint64_t metric_emission_failure_total_ = 0U;
  std::optional<dasall::contracts::ResultCode> last_log_error_code_;
  std::optional<infra::metrics::MetricsErrorCode> last_metrics_error_code_;
};

[[nodiscard]] infra::metrics::MetricIdentity make_llm_metric_identity(
    LLMMetricKind kind);

}  // namespace dasall::llm::observability