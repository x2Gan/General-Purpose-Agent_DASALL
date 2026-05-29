#include "LLMMetricsBridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace dasall::llm::observability {
namespace {

constexpr std::string_view kLLMMetricsBridgeLogStage = "llm.observability.log";
constexpr std::string_view kLLMMetricsBridgeMetricsStage =
    "llm.observability.metrics";
constexpr std::string_view kLLMMetricsBridgeSourceRef = "LLMMetricsBridge";
constexpr std::array<std::string_view, 13> kSensitiveValuePrefixes = {
    "bearer ",
    "token=",
    "token:",
    "secret=",
    "secret:",
    "password=",
    "password:",
    "authorization=",
    "authorization:",
    "api_key=",
    "apikey=",
    "api-key=",
    "x-api-key=",
};

[[nodiscard]] bool is_value_delimiter(char ch) {
  switch (ch) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case ',':
    case ';':
    case '&':
    case ')':
    case '(':
    case ']':
    case '[':
    case '}':
    case '{':
    case '"':
    case '\'':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::string lower_copy(std::string_view text) {
  std::string lowered(text);
  for (auto& ch : lowered) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  return lowered;
}

void redact_prefix_payload(std::string& text, std::string_view prefix) {
  constexpr std::string_view kAuthorizationPrefix = "authorization:";
  constexpr std::string_view kAuthorizationEquals = "authorization=";
  constexpr std::string_view kBearerPrefix = "bearer ";

  std::size_t search_pos = 0;
  while (search_pos < text.size()) {
    const auto lowered = lower_copy(text);
    const auto match_pos = lowered.find(prefix, search_pos);
    if (match_pos == std::string::npos) {
      break;
    }

    auto value_start = match_pos + prefix.size();
    while (value_start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[value_start])) != 0) {
      ++value_start;
    }

    if ((prefix == kAuthorizationPrefix || prefix == kAuthorizationEquals) &&
        value_start < text.size()) {
      const auto authorization_value =
          lower_copy(std::string_view(text).substr(value_start));
      if (authorization_value.rfind(kBearerPrefix, 0) == 0) {
        value_start += kBearerPrefix.size();
      }
    }

    auto value_end = value_start;
    while (value_end < text.size() && !is_value_delimiter(text[value_end])) {
      ++value_end;
    }

    if (value_end == value_start) {
      search_pos = value_start + 1U;
      continue;
    }

    text.replace(value_start,
                 value_end - value_start,
                 std::string(infra::logging::LogEvent::kRedactedValue));
    search_pos = value_start + infra::logging::LogEvent::kRedactedValue.size();
  }
}

[[nodiscard]] std::string redact_sensitive_log_values(std::string value) {
  for (const auto prefix : kSensitiveValuePrefixes) {
    redact_prefix_payload(value, prefix);
  }

  return value;
}

[[nodiscard]] std::string normalize_token(std::string_view value) {
  if (value.empty()) {
    return "unknown";
  }

  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    const auto code = static_cast<unsigned char>(ch);
    if (std::isalnum(code) || ch == '_' || ch == '.' || ch == '-' || ch == '/') {
      normalized.push_back(ch);
    } else {
      normalized.push_back('_');
    }
  }

  return normalized.empty() ? std::string("unknown") : normalized;
}

[[nodiscard]] std::string join_values(const std::vector<std::string>& values,
                                      std::string_view delimiter) {
  std::string joined;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      joined.append(delimiter);
    }
    joined.append(values[index]);
  }

  return joined;
}

[[nodiscard]] std::string sanitize_log_text(std::string_view value,
                                            std::size_t max_length = 256U) {
  std::string sanitized;
  sanitized.reserve(std::min(value.size(), max_length));
  for (const char ch : value) {
    const auto code = static_cast<unsigned char>(ch);
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      sanitized.push_back(' ');
    } else if (std::iscntrl(code)) {
      sanitized.push_back('_');
    } else {
      sanitized.push_back(ch);
    }

    if (sanitized.size() >= max_length) {
      break;
    }
  }

  auto redacted = redact_sensitive_log_values(std::move(sanitized));
  if (redacted.size() > max_length) {
    redacted.resize(max_length);
  }

  return redacted;
}

[[nodiscard]] std::string primary_reason_code(
    const LLMCallSummary& summary) {
  if (summary.selection_reason_codes.empty()) {
    return "none";
  }

  return normalize_token(summary.selection_reason_codes.front());
}

[[nodiscard]] std::string normalized_profile_id(const LLMCallSummary& summary) {
  return summary.profile_id.empty() ? std::string("unknown")
                                    : normalize_token(summary.profile_id);
}

[[nodiscard]] std::string normalized_error_code(const LLMCallSummary& summary) {
  if (summary.error_type.empty()) {
    return std::string(kLLMMetricNoErrorCodeLabel);
  }

  return normalize_token(summary.error_type);
}

[[nodiscard]] std::string format_cost(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(6) << value;
  return stream.str();
}

[[nodiscard]] std::string llm_metric_description(LLMMetricKind kind) {
  switch (kind) {
    case LLMMetricKind::CallsTotal:
      return "llm call totals by stage and resolved route";
    case LLMMetricKind::CallLatencyMs:
      return "llm call latency in milliseconds by stage and model";
    case LLMMetricKind::FallbackTotal:
      return "llm fallback transitions by stage and route pair";
    case LLMMetricKind::ModelSelectionTotal:
      return "llm model selection totals by stage, effective reasoning mode and primary reason";
    case LLMMetricKind::ReasoningEscalationTotal:
      return "llm reasoning escalation totals by stage and primary reason";
    case LLMMetricKind::PromptPolicyDenyTotal:
      return "llm prompt policy deny totals by stage and primary reason";
    case LLMMetricKind::PromptComposeOverBudgetTotal:
      return "llm prompt compose over-budget totals by stage";
    case LLMMetricKind::AdapterTimeoutTotal:
      return "llm adapter timeout totals by stage and route";
    case LLMMetricKind::HealthDegradedTotal:
      return "llm health degraded totals by stage and route";
    case LLMMetricKind::PromptCacheHitTokensTotal:
      return "llm prompt cache hit token totals by provider and model";
    case LLMMetricKind::PromptCacheMissTokensTotal:
      return "llm prompt cache miss token totals by provider and model";
    case LLMMetricKind::CostEstimateUsdTotal:
      return "llm estimated cost totals in usd by provider and model";
  }

  return "llm observability metric";
}

[[nodiscard]] infra::metrics::MetricsOperationStatus make_metrics_failure_status(
    infra::metrics::MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = infra::metrics::map_metrics_error_code(error_code);
  return infra::metrics::MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kLLMMetricsBridgeMetricsStage),
      std::string(kLLMMetricsBridgeSourceRef));
}

[[nodiscard]] bool metrics_error_causes_degraded(
    infra::metrics::MetricsErrorCode error_code) {
  switch (error_code) {
    case infra::metrics::MetricsErrorCode::ProviderNotReady:
    case infra::metrics::MetricsErrorCode::IdentityInvalid:
    case infra::metrics::MetricsErrorCode::ExportFailure:
    case infra::metrics::MetricsErrorCode::ExportTimeout:
    case infra::metrics::MetricsErrorCode::ConfigInvalid:
      return true;
    case infra::metrics::MetricsErrorCode::LabelCardinalityExceeded:
    case infra::metrics::MetricsErrorCode::QueueFull:
      return false;
  }

  return true;
}

[[nodiscard]] infra::metrics::MetricsErrorCode infer_metrics_error_code(
    const infra::metrics::MetricsOperationStatus& status,
    infra::metrics::MetricsErrorCode fallback) {
  switch (status.result_code) {
    case contracts::ResultCode::ToolExecutionFailed:
      return fallback;
    case contracts::ResultCode::ProviderTimeout:
      return infra::metrics::MetricsErrorCode::ExportFailure;
    case contracts::ResultCode::PolicyDenied:
      return infra::metrics::MetricsErrorCode::LabelCardinalityExceeded;
    case contracts::ResultCode::ValidationFieldMissing:
      return infra::metrics::MetricsErrorCode::ConfigInvalid;
    case contracts::ResultCode::RuntimeRetryExhausted:
      return infra::metrics::MetricsErrorCode::QueueFull;
  }

  return fallback;
}

[[nodiscard]] std::string metric_stage_token(LLMMetricKind kind,
                                             const LLMCallSummary& summary) {
  const std::string stage = normalize_token(summary.stage);
  const std::string route = normalize_token(summary.resolved_route);
  const std::string model = normalize_token(summary.model_name);
  const std::string provider = normalize_token(summary.provider_id);
  const std::string reason = primary_reason_code(summary);
  const std::string reasoning_mode =
      normalize_token(summary.reasoning_mode_effective);

  switch (kind) {
    case LLMMetricKind::CallsTotal:
      return "call/" + stage + "/" + route;
    case LLMMetricKind::CallLatencyMs:
      return "latency/" + stage + "/" + model;
    case LLMMetricKind::FallbackTotal:
      return "fallback/" + stage + "/" + normalize_token(summary.from_route) +
             "_to_" + normalize_token(summary.to_route);
    case LLMMetricKind::ModelSelectionTotal:
      return "selection/" + stage + "/" + model + "/" + reasoning_mode +
             "/" + reason;
    case LLMMetricKind::ReasoningEscalationTotal:
      return "reasoning_escalation/" + stage + "/" + reason;
    case LLMMetricKind::PromptPolicyDenyTotal:
      return "prompt_policy_deny/" + stage + "/" + reason;
    case LLMMetricKind::PromptComposeOverBudgetTotal:
      return "prompt_over_budget/" + stage;
    case LLMMetricKind::AdapterTimeoutTotal:
      return "adapter_timeout/" + stage + "/" + route;
    case LLMMetricKind::HealthDegradedTotal:
      return "health_degraded/" + stage + "/" + route;
    case LLMMetricKind::PromptCacheHitTokensTotal:
      return "cache_hit/" + provider + "/" + model;
    case LLMMetricKind::PromptCacheMissTokensTotal:
      return "cache_miss/" + provider + "/" + model;
    case LLMMetricKind::CostEstimateUsdTotal:
      return "cost/" + provider + "/" + model;
  }

  return "llm/unknown";
}

[[nodiscard]] std::string metric_outcome(LLMMetricKind kind,
                                         const LLMCallSummary& summary) {
  switch (kind) {
    case LLMMetricKind::FallbackTotal:
      return summary.outcome == "failure" ? std::string("failure")
                                           : std::string("degraded");
    case LLMMetricKind::PromptPolicyDenyTotal:
    case LLMMetricKind::PromptComposeOverBudgetTotal:
      return "rejected";
    case LLMMetricKind::AdapterTimeoutTotal:
      return "failure";
    case LLMMetricKind::HealthDegradedTotal:
      return "degraded";
    case LLMMetricKind::CallsTotal:
    case LLMMetricKind::CallLatencyMs:
    case LLMMetricKind::ModelSelectionTotal:
    case LLMMetricKind::ReasoningEscalationTotal:
    case LLMMetricKind::PromptCacheHitTokensTotal:
    case LLMMetricKind::PromptCacheMissTokensTotal:
    case LLMMetricKind::CostEstimateUsdTotal:
      return summary.outcome;
  }

  return summary.outcome;
}

[[nodiscard]] double metric_value(LLMMetricKind kind,
                                  const LLMCallSummary& summary) {
  switch (kind) {
    case LLMMetricKind::CallsTotal:
    case LLMMetricKind::FallbackTotal:
    case LLMMetricKind::ModelSelectionTotal:
    case LLMMetricKind::ReasoningEscalationTotal:
    case LLMMetricKind::PromptPolicyDenyTotal:
    case LLMMetricKind::PromptComposeOverBudgetTotal:
    case LLMMetricKind::AdapterTimeoutTotal:
    case LLMMetricKind::HealthDegradedTotal:
      return 1.0;
    case LLMMetricKind::CallLatencyMs:
      return static_cast<double>(summary.latency_ms);
    case LLMMetricKind::PromptCacheHitTokensTotal:
      return static_cast<double>(summary.prompt_cache_hit_tokens);
    case LLMMetricKind::PromptCacheMissTokensTotal:
      return static_cast<double>(summary.prompt_cache_miss_tokens);
    case LLMMetricKind::CostEstimateUsdTotal:
      return summary.actual_cost_estimate_usd;
  }

  return 0.0;
}

[[nodiscard]] infra::metrics::MetricSample make_metric_sample(
    LLMMetricKind kind,
    const LLMCallSummary& summary) {
  return infra::metrics::MetricSample{
      .identity_ref = make_llm_metric_identity(kind),
      .value = metric_value(kind, summary),
      .ts_unix_ms = summary.completed_at_ms,
      .labels = infra::metrics::MetricLabels{
          .module = std::string(kLLMMetricModuleLabel),
          .stage = metric_stage_token(kind, summary),
          .profile = normalized_profile_id(summary),
          .outcome = metric_outcome(kind, summary),
          .error_code = normalized_error_code(summary),
      },
  };
}

}  // namespace

infra::metrics::MetricIdentity make_llm_metric_identity(LLMMetricKind kind) {
  return infra::metrics::MetricIdentity{
      .name = std::string(llm_metric_name(kind)),
      .type = llm_metric_type(kind),
      .unit = std::string(llm_metric_unit(kind)),
      .description = llm_metric_description(kind),
  };
}

bool LLMCallSummary::has_consistent_values() const {
  if (request_id.empty() || llm_call_id.empty() || stage.empty() ||
      resolved_route.empty() || model_name.empty() || prompt_id.empty() ||
      prompt_version.empty() || reasoning_mode_requested.empty() ||
      reasoning_mode_effective.empty() || provider_id.empty() ||
      selection_reason_codes.empty() || completed_at_ms <= 0 ||
      !is_llm_metric_outcome(outcome) || !std::isfinite(actual_cost_estimate_usd) ||
      actual_cost_estimate_usd < 0.0) {
    return false;
  }

  for (std::size_t index = 0; index < selection_reason_codes.size(); ++index) {
    if (selection_reason_codes[index].empty()) {
      return false;
    }

    if (std::find(selection_reason_codes.begin() +
                      static_cast<std::ptrdiff_t>(index + 1),
                  selection_reason_codes.end(),
                  selection_reason_codes[index]) != selection_reason_codes.end()) {
      return false;
    }
  }

  if (fallback_used &&
      (from_route.empty() || to_route.empty() || to_route != resolved_route)) {
    return false;
  }

  return true;
}

bool LLMMetricsBridgeStatus::is_valid() const {
  if (last_log_error_code.has_value() &&
      contracts::classify_result_code(*last_log_error_code) ==
          contracts::ResultCodeCategory::Unknown) {
    return false;
  }

  return true;
}

LLMMetricsBridge::LLMMetricsBridge(
    std::shared_ptr<infra::logging::ILogger> logger,
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider)
    : logger_(std::move(logger)), metrics_provider_(std::move(metrics_provider)) {}

void LLMMetricsBridge::set_logger(std::shared_ptr<infra::logging::ILogger> logger) {
  logger_ = std::move(logger);
}

void LLMMetricsBridge::set_metrics_provider(
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider) {
  metrics_provider_ = std::move(metrics_provider);
  meter_.reset();
  instrument_handles_.fill(std::nullopt);
  instruments_registered_ = false;
}

LLMLoggingWriteResult LLMMetricsBridge::write_call_log(
    const LLMCallSummary& summary) {
  if (!summary.has_consistent_values()) {
    record_log_failure(contracts::ResultCode::ValidationFieldMissing);
    return LLMLoggingWriteResult{
        .emitted = false,
        .log_event = {},
        .write_result = infra::logging::LogWriteResult::failure(
            contracts::ResultCode::ValidationFieldMissing,
            "llm metrics bridge requires a valid call summary before structured logging",
            std::string(kLLMMetricsBridgeLogStage),
            std::string(kLLMMetricsBridgeSourceRef)),
    };
  }

  if (!logger_) {
    record_log_failure(contracts::ResultCode::RuntimeRetryExhausted);
    return LLMLoggingWriteResult{
        .emitted = false,
        .log_event = {},
        .write_result = infra::logging::LogWriteResult::failure(
            contracts::ResultCode::RuntimeRetryExhausted,
            "llm metrics bridge requires an ILogger sink before structured logging",
            std::string(kLLMMetricsBridgeLogStage),
            std::string(kLLMMetricsBridgeSourceRef)),
    };
  }

  auto event = make_log_event(summary);
  if (!event.attrs_are_serializable() || !event.has_timestamp()) {
    record_log_failure(contracts::ResultCode::ValidationFieldMissing);
    return LLMLoggingWriteResult{
        .emitted = false,
        .log_event = std::move(event),
        .write_result = infra::logging::LogWriteResult::failure(
            contracts::ResultCode::ValidationFieldMissing,
            "llm metrics bridge produced a non-serializable log payload",
            std::string(kLLMMetricsBridgeLogStage),
            std::string(kLLMMetricsBridgeSourceRef)),
    };
  }

  const auto write_result = logger_->log(event);
  if (!write_result.ok) {
    record_log_failure(write_result.result_code);
    return LLMLoggingWriteResult{
        .emitted = false,
        .log_event = event,
        .write_result = write_result,
    };
  }

  record_log_success();
  return LLMLoggingWriteResult{
      .emitted = true,
      .log_event = event,
      .write_result = write_result,
  };
}

LLMMetricsEmitResult LLMMetricsBridge::record_call_metrics(
    const LLMCallSummary& summary) {
  ++metric_emission_attempt_total_;

  if (!summary.has_consistent_values()) {
    return make_metrics_failure_result(
        infra::metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            infra::metrics::MetricsErrorCode::ConfigInvalid,
            "llm metrics bridge requires a valid call summary before metrics emission"));
  }

  auto samples = make_metric_samples(summary);
  if (samples.empty()) {
    return make_metrics_failure_result(
        infra::metrics::MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(
            infra::metrics::MetricsErrorCode::ConfigInvalid,
            "llm metrics bridge did not produce any metric samples for the call summary"));
  }

  for (const auto& sample : samples) {
    if (!sample.is_valid()) {
      return make_metrics_failure_result(
          infra::metrics::MetricsErrorCode::ConfigInvalid,
          make_metrics_failure_status(
              infra::metrics::MetricsErrorCode::ConfigInvalid,
              "llm metrics bridge produced an invalid metric sample"));
    }
  }

  LLMMetricsEmitResult failure;
  if (!ensure_meter_ready(&failure)) {
    return failure;
  }

  if (!ensure_instruments_registered(&failure)) {
    return failure;
  }

  for (const auto& sample : samples) {
    const auto status = meter_->record(sample);
    if (!status.ok) {
      return make_metrics_failure_result(
          infer_metrics_error_code(status,
                                   infra::metrics::MetricsErrorCode::ExportFailure),
          status);
    }
  }

  degraded_ = false;
  last_metrics_error_code_.reset();
  return LLMMetricsEmitResult{
      .emitted = true,
      .emitted_samples = samples.size(),
      .bridge_degraded = false,
      .status = infra::metrics::MetricsOperationStatus::success(
          "llm-observability://metrics"),
      .metrics_error_code = std::nullopt,
  };
}

LLMObservabilityRecordResult LLMMetricsBridge::record_call(
    const LLMCallSummary& summary) {
  const auto log_result = write_call_log(summary);
  const auto metrics_result = record_call_metrics(summary);
  return LLMObservabilityRecordResult{
      .emitted = log_result.emitted && metrics_result.emitted,
      .log_result = log_result,
      .metrics_result = metrics_result,
  };
}

LLMMetricsBridgeStatus LLMMetricsBridge::get_status() const {
  return LLMMetricsBridgeStatus{
      .log_emitted_total = log_emitted_total_,
      .log_emit_failures = log_emit_failures_,
      .metric_emission_attempt_total = metric_emission_attempt_total_,
      .metric_emission_failure_total = metric_emission_failure_total_,
      .degraded = degraded_ || log_emit_failures_ > 0U ||
                  last_metrics_error_code_.has_value() ||
                  last_log_error_code_.has_value(),
      .has_active_meter = static_cast<bool>(meter_),
      .instruments_registered = instruments_registered_,
      .last_log_error_code = last_log_error_code_,
      .last_metrics_error_code = last_metrics_error_code_,
  };
}

bool LLMMetricsBridge::ensure_meter_ready(LLMMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_metrics_failure_result(
        infra::metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            infra::metrics::MetricsErrorCode::ProviderNotReady,
            "llm metrics bridge requires a metrics provider before emitting samples"));
    return false;
  }

  auto meter = metrics_provider_->get_meter(infra::metrics::MeterScope{
      .name = std::string(kLLMMetricsMeterScopeName),
      .version = std::string(kLLMMetricsMeterScopeVersion),
      .schema_url = std::string(),
  });
  if (!meter) {
    *failure = make_metrics_failure_result(
        infra::metrics::MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(
            infra::metrics::MetricsErrorCode::ProviderNotReady,
            "metrics provider did not supply the llm.observability meter scope"));
    return false;
  }

  meter_ = std::move(meter);
  return true;
}

bool LLMMetricsBridge::ensure_instruments_registered(
    LLMMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  instrument_handles_.fill(std::nullopt);
  for (std::size_t index = 0; index < kLLMMetricFamilyCount; ++index) {
    const auto kind = static_cast<LLMMetricKind>(index);
    const auto identity = make_llm_metric_identity(kind);
    std::optional<infra::metrics::InstrumentHandle> handle;
    switch (llm_metric_type(kind)) {
      case infra::metrics::MetricType::Counter:
        handle = meter_->create_counter(identity);
        break;
      case infra::metrics::MetricType::Histogram:
        handle = meter_->create_histogram(identity);
        break;
      case infra::metrics::MetricType::Gauge:
        handle = meter_->create_gauge(identity);
        break;
      case infra::metrics::MetricType::UpDownCounter:
        handle = std::nullopt;
        break;
    }

    if (!handle.has_value() || !handle->is_valid()) {
      instrument_handles_.fill(std::nullopt);
      instruments_registered_ = false;
      meter_.reset();
      *failure = make_metrics_failure_result(
          infra::metrics::MetricsErrorCode::IdentityInvalid,
          make_metrics_failure_status(
              infra::metrics::MetricsErrorCode::IdentityInvalid,
              "llm metrics bridge could not register metric family " +
                  identity.name));
      return false;
    }

    instrument_handles_[index] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

infra::logging::LogEvent LLMMetricsBridge::make_log_event(
    const LLMCallSummary& summary) const {
  infra::logging::LogEvent::AttributeMap attrs;
  attrs.emplace("request_id", summary.request_id);
  attrs.emplace("llm_call_id", summary.llm_call_id);
  attrs.emplace("request_mode", summary.request_mode);
  attrs.emplace("stage", summary.stage);
  attrs.emplace("resolved_route", summary.resolved_route);
  attrs.emplace("model_name", summary.model_name);
  attrs.emplace("prompt_id", summary.prompt_id);
  attrs.emplace("prompt_version", summary.prompt_version);
  attrs.emplace("fallback_used", summary.fallback_used ? "true" : "false");
  attrs.emplace("latency_ms", std::to_string(summary.latency_ms));
  attrs.emplace("failure_category",
                summary.failure_category.empty() ? std::string("none")
                                                 : summary.failure_category);
  attrs.emplace("selection_reason_codes",
                join_values(summary.selection_reason_codes, ","));
  attrs.emplace("estimated_input_tokens",
                std::to_string(summary.estimated_input_tokens));
  attrs.emplace("prompt_cache_hit_tokens",
                std::to_string(summary.prompt_cache_hit_tokens));
  attrs.emplace("prompt_cache_miss_tokens",
                std::to_string(summary.prompt_cache_miss_tokens));
  attrs.emplace("actual_cost_estimate_usd",
                format_cost(summary.actual_cost_estimate_usd));
  attrs.emplace("reasoning_mode_requested", summary.reasoning_mode_requested);
  attrs.emplace("reasoning_mode_effective", summary.reasoning_mode_effective);
  attrs.emplace("error_type",
                summary.error_type.empty() ? std::string("none")
                                           : summary.error_type);
  attrs.emplace("result_code",
                summary.result_code.empty() ? std::string("none")
                                            : summary.result_code);
  attrs.emplace("result_code_category",
                summary.result_code_category.empty()
                    ? std::string("none")
                    : summary.result_code_category);
  attrs.emplace("error_stage",
                summary.error_stage.empty() ? std::string("none")
                                            : sanitize_log_text(summary.error_stage));
  attrs.emplace("error_message",
                summary.error_message.empty()
                    ? std::string("none")
                    : sanitize_log_text(summary.error_message));
  attrs.emplace("provider_id", summary.provider_id);
  attrs.emplace("profile_id",
                summary.profile_id.empty() ? std::string("unknown")
                                           : summary.profile_id);
  attrs.emplace("outcome", summary.outcome);
  attrs.emplace("attempted_routes", join_values(summary.attempted_routes, ","));
  attrs.emplace("route_attempt_count",
                std::to_string(summary.attempted_routes.size()));
  attrs.emplace("source_ref_type",
                summary.source_ref_type.empty() ? std::string("none")
                                                : sanitize_log_text(summary.source_ref_type));
  attrs.emplace("source_ref_id",
                summary.source_ref_id.empty() ? std::string("none")
                                              : sanitize_log_text(summary.source_ref_id));
  attrs.emplace("retryable", summary.retryable ? "true" : "false");
  attrs.emplace("safe_to_replan", summary.safe_to_replan ? "true" : "false");
  attrs.emplace("governance_disposition",
                summary.governance_disposition.empty()
                    ? std::string("none")
                    : summary.governance_disposition);

  if (summary.fallback_used) {
    attrs.emplace("from_route", summary.from_route);
    attrs.emplace("to_route", summary.to_route);
  }

  return infra::logging::LogEvent{
      .level = summary.outcome == "failure"
                   ? infra::logging::LogLevel::Error
             : (summary.outcome == "degraded" || summary.outcome == "rejected"
                          ? infra::logging::LogLevel::Warn
                          : infra::logging::LogLevel::Info),
      .module = std::string("llm"),
      .message = std::string("llm call ") + summary.outcome + " stage=" +
                 summary.stage + " route=" + summary.resolved_route,
      .attrs = std::move(attrs),
      .ts = summary.completed_at_ms,
  };
}

std::vector<infra::metrics::MetricSample> LLMMetricsBridge::make_metric_samples(
    const LLMCallSummary& summary) const {
  std::vector<infra::metrics::MetricSample> samples;
  samples.reserve(kLLMMetricFamilyCount);

  samples.push_back(make_metric_sample(LLMMetricKind::CallsTotal, summary));
  samples.push_back(make_metric_sample(LLMMetricKind::CallLatencyMs, summary));
  samples.push_back(make_metric_sample(LLMMetricKind::ModelSelectionTotal, summary));

  if (summary.fallback_used) {
    samples.push_back(make_metric_sample(LLMMetricKind::FallbackTotal, summary));
  }

  if (summary.reasoning_escalated) {
    samples.push_back(
        make_metric_sample(LLMMetricKind::ReasoningEscalationTotal, summary));
  }

  if (summary.prompt_policy_denied) {
    samples.push_back(
        make_metric_sample(LLMMetricKind::PromptPolicyDenyTotal, summary));
  }

  if (summary.prompt_compose_over_budget) {
    samples.push_back(make_metric_sample(
        LLMMetricKind::PromptComposeOverBudgetTotal, summary));
  }

  if (summary.adapter_timeout) {
    samples.push_back(make_metric_sample(LLMMetricKind::AdapterTimeoutTotal, summary));
  }

  if (summary.health_degraded) {
    samples.push_back(make_metric_sample(LLMMetricKind::HealthDegradedTotal, summary));
  }

  samples.push_back(
      make_metric_sample(LLMMetricKind::PromptCacheHitTokensTotal, summary));
  samples.push_back(
      make_metric_sample(LLMMetricKind::PromptCacheMissTokensTotal, summary));
  samples.push_back(make_metric_sample(LLMMetricKind::CostEstimateUsdTotal, summary));

  return samples;
}

LLMMetricsEmitResult LLMMetricsBridge::make_metrics_failure_result(
    infra::metrics::MetricsErrorCode error_code,
    infra::metrics::MetricsOperationStatus status) {
  ++metric_emission_failure_total_;
  last_metrics_error_code_ = error_code;

  const bool failure_causes_degraded = metrics_error_causes_degraded(error_code);
  degraded_ = degraded_ || failure_causes_degraded;

  if (error_code == infra::metrics::MetricsErrorCode::ProviderNotReady ||
      error_code == infra::metrics::MetricsErrorCode::IdentityInvalid ||
      error_code == infra::metrics::MetricsErrorCode::ConfigInvalid) {
    meter_.reset();
    instrument_handles_.fill(std::nullopt);
    instruments_registered_ = false;
  }

  return LLMMetricsEmitResult{
      .emitted = false,
      .emitted_samples = 0U,
      .bridge_degraded = failure_causes_degraded,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

void LLMMetricsBridge::record_log_success() {
  ++log_emitted_total_;
  last_log_error_code_.reset();
}

void LLMMetricsBridge::record_log_failure(
    std::optional<contracts::ResultCode> result_code) {
  ++log_emit_failures_;
  last_log_error_code_ = result_code;
  degraded_ = degraded_ || result_code.has_value();
}

}  // namespace dasall::llm::observability