#include "ops/ToolMetricsBridge.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <utility>

#include "RuntimePolicySnapshot.h"
#include "metrics/MetricTypes.h"

namespace dasall::tools::ops {

struct ToolMetricsBridge::ToolMetricSignal {
  ToolMetricKind kind = ToolMetricKind::request_total;
  double value = 0.0;
  std::int64_t ts_unix_ms = 0;
  std::string stage;
  std::string profile;
  std::string outcome;
  std::string error_code;
};

namespace {

using infra::metrics::MetricIdentity;
using infra::metrics::MetricLabels;
using infra::metrics::MetricSample;
using infra::metrics::MetricType;
using infra::metrics::MetricsErrorCode;
using infra::metrics::MetricsOperationStatus;

constexpr std::string_view kToolMetricsBridgeSourceRef = "ToolMetricsBridge";
constexpr std::string_view kToolMetricsBridgeStage = "tools.metrics_bridge";
constexpr std::array<ToolMetricKind, kToolMetricFamilyCount> kToolMetricKinds{
    ToolMetricKind::request_total,
    ToolMetricKind::admission_denied_total,
    ToolMetricKind::execution_latency_ms,
    ToolMetricKind::partial_side_effect_total,
    ToolMetricKind::mcp_stale_snapshot_total,
    ToolMetricKind::workflow_step_failure_total,
};

enum class ToolMetricsGranularity {
  full = 0,
  partial,
  minimal,
};

[[nodiscard]] std::size_t to_index(ToolMetricKind kind) {
  return static_cast<std::size_t>(kind);
}

[[nodiscard]] std::string normalize_profile_id(std::string profile_id) {
  if (profile_id.empty()) {
    return "unknown";
  }

  return profile_id;
}

[[nodiscard]] std::string normalize_granularity(std::string granularity) {
  if (granularity == "partial" || granularity == "minimal") {
    return granularity;
  }

  return "full";
}

[[nodiscard]] ToolMetricsGranularity parse_granularity(
    const std::string_view& granularity) {
  if (granularity == "minimal") {
    return ToolMetricsGranularity::minimal;
  }

  if (granularity == "partial") {
    return ToolMetricsGranularity::partial;
  }

  return ToolMetricsGranularity::full;
}

[[nodiscard]] std::string normalized_or(std::string value,
                                        std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] std::string sanitize_stage_token(std::string_view value) {
  if (value.empty()) {
    return "unknown";
  }

  std::string token;
  token.reserve(value.size());
  for (const char ch : value) {
    const auto code = static_cast<unsigned char>(ch);
    if (std::isalnum(code) || ch == '.' || ch == '_' || ch == '-') {
      token.push_back(ch);
      continue;
    }

    token.push_back('_');
  }

  return token.empty() ? std::string("unknown") : token;
}

[[nodiscard]] std::string join_stage(std::initializer_list<std::string> parts) {
  std::string stage;
  bool first = true;
  for (const auto& part : parts) {
    if (part.empty()) {
      continue;
    }

    if (!first) {
      stage.push_back('.');
    }

    stage += part;
    first = false;
  }

  return stage.empty() ? std::string("tools.unknown") : stage;
}

[[nodiscard]] std::string tool_category_token(
    const std::optional<contracts::ToolDescriptor>& descriptor) {
  if (!descriptor.has_value() || !descriptor->category.has_value()) {
    return "unknown";
  }

  switch (*descriptor->category) {
    case contracts::ToolCategory::Information:
      return "information";
    case contracts::ToolCategory::Action:
      return "action";
    case contracts::ToolCategory::Workflow:
      return "workflow";
    case contracts::ToolCategory::AgentDelegation:
      return "agent_delegation";
    case contracts::ToolCategory::Diagnostic:
      return "diagnostic";
    case contracts::ToolCategory::Unspecified:
      break;
  }

  return "unknown";
}

[[nodiscard]] std::string tool_name_token(
    const contracts::ToolRequest& request,
    const std::optional<contracts::ToolDescriptor>& descriptor) {
  if (descriptor.has_value() && descriptor->tool_name.has_value() &&
      !descriptor->tool_name->empty()) {
    return sanitize_stage_token(*descriptor->tool_name);
  }

  if (request.tool_name.has_value() && !request.tool_name->empty()) {
    return sanitize_stage_token(*request.tool_name);
  }

  return "unknown";
}

[[nodiscard]] std::string route_kind_token(
    const std::optional<std::string>& route_kind) {
  if (route_kind.has_value() && !route_kind->empty()) {
    return sanitize_stage_token(*route_kind);
  }

  return "unrouted";
}

[[nodiscard]] std::string outcome_for_failure(const ToolInvocationEnvelope& envelope) {
  if (envelope.tool_result.has_value() && envelope.tool_result->error.has_value() &&
      envelope.tool_result->error->failure_type.has_value() &&
      *envelope.tool_result->error->failure_type ==
          contracts::ResultCodeCategory::Policy) {
    return "rejected";
  }

  return "failure";
}

[[nodiscard]] std::string outcome_for_terminal(const ToolInvocationEnvelope& envelope) {
  if (!envelope.tool_result.has_value()) {
    return "failure";
  }

  if (envelope.tool_result->success.value_or(false)) {
    return "success";
  }

  if (envelope.tool_result->side_effects.has_value() &&
      !envelope.tool_result->side_effects->empty()) {
    return "degraded";
  }

  return outcome_for_failure(envelope);
}

[[nodiscard]] std::string error_code_label_from_envelope(
    const ToolInvocationEnvelope& envelope) {
  if (envelope.failure_reason_code.has_value() &&
      !envelope.failure_reason_code->empty()) {
    return sanitize_stage_token(*envelope.failure_reason_code);
  }

  if (envelope.tool_result.has_value() && envelope.tool_result->error.has_value() &&
      envelope.tool_result->error->details.code.has_value()) {
    return std::to_string(*envelope.tool_result->error->details.code);
  }

  return std::string(kToolMetricNoErrorCodeLabel);
}

[[nodiscard]] std::string result_code_label(contracts::ResultCode result_code) {
  switch (result_code) {
    case contracts::ResultCode::ValidationFieldMissing:
      return "ValidationFieldMissing";
    case contracts::ResultCode::PolicyDenied:
      return "PolicyDenied";
    case contracts::ResultCode::ToolExecutionFailed:
      return "ToolExecutionFailed";
    case contracts::ResultCode::ProviderTimeout:
      return "ProviderTimeout";
    case contracts::ResultCode::RuntimeRetryExhausted:
      return "RuntimeRetryExhausted";
  }

  return "Unknown";
}

[[nodiscard]] std::string profile_id_for(const ToolInvocationContext& context,
                                         std::string fallback) {
  if (context.profile_snapshot != nullptr) {
    return normalize_profile_id(context.profile_snapshot->effective_profile_id());
  }

  return normalize_profile_id(std::move(fallback));
}

[[nodiscard]] MetricType metric_type(ToolMetricKind kind) {
  switch (kind) {
    case ToolMetricKind::execution_latency_ms:
      return MetricType::Histogram;
    case ToolMetricKind::request_total:
    case ToolMetricKind::admission_denied_total:
    case ToolMetricKind::partial_side_effect_total:
    case ToolMetricKind::mcp_stale_snapshot_total:
    case ToolMetricKind::workflow_step_failure_total:
      return MetricType::Counter;
  }

  return MetricType::Counter;
}

[[nodiscard]] std::string metric_name(ToolMetricKind kind) {
  switch (kind) {
    case ToolMetricKind::request_total:
      return "tool_request_total";
    case ToolMetricKind::admission_denied_total:
      return "tool_admission_denied_total";
    case ToolMetricKind::execution_latency_ms:
      return "tool_execution_latency_ms";
    case ToolMetricKind::partial_side_effect_total:
      return "tool_partial_side_effect_total";
    case ToolMetricKind::mcp_stale_snapshot_total:
      return "tool_mcp_stale_snapshot_total";
    case ToolMetricKind::workflow_step_failure_total:
      return "tool_workflow_step_failure_total";
  }

  return "tool_metric_unknown";
}

[[nodiscard]] std::string metric_unit(ToolMetricKind kind) {
  return kind == ToolMetricKind::execution_latency_ms ? "ms" : "1";
}

[[nodiscard]] std::string metric_description(ToolMetricKind kind) {
  switch (kind) {
    case ToolMetricKind::request_total:
      return "tool request total";
    case ToolMetricKind::admission_denied_total:
      return "tool admission denied total";
    case ToolMetricKind::execution_latency_ms:
      return "tool execution latency in milliseconds";
    case ToolMetricKind::partial_side_effect_total:
      return "tool partial side effect total";
    case ToolMetricKind::mcp_stale_snapshot_total:
      return "tool mcp stale snapshot total";
    case ToolMetricKind::workflow_step_failure_total:
      return "tool workflow step failure total";
  }

  return "tool metric";
}

[[nodiscard]] MetricIdentity make_metric_identity(ToolMetricKind kind) {
  return MetricIdentity{
      .name = metric_name(kind),
      .type = metric_type(kind),
      .unit = metric_unit(kind),
      .description = metric_description(kind),
  };
}

[[nodiscard]] MetricsOperationStatus make_metrics_failure_status(
    MetricsErrorCode error_code,
    std::string message) {
  const auto mapping = infra::metrics::map_metrics_error_code(error_code);
  return MetricsOperationStatus::failure(mapping.result_code,
                                         std::move(message),
                                         std::string(kToolMetricsBridgeStage),
                                         std::string(kToolMetricsBridgeSourceRef));
}

[[nodiscard]] bool metrics_error_causes_degraded(MetricsErrorCode error_code) {
  switch (error_code) {
    case MetricsErrorCode::ProviderNotReady:
    case MetricsErrorCode::IdentityInvalid:
    case MetricsErrorCode::ExportFailure:
    case MetricsErrorCode::ExportTimeout:
    case MetricsErrorCode::ConfigInvalid:
      return true;
    case MetricsErrorCode::LabelCardinalityExceeded:
    case MetricsErrorCode::QueueFull:
      return false;
  }

  return true;
}

[[nodiscard]] MetricsErrorCode infer_metrics_error_code(
    const MetricsOperationStatus& status,
    MetricsErrorCode fallback) {
  switch (status.result_code) {
    case contracts::ResultCode::ProviderTimeout:
      return MetricsErrorCode::ExportFailure;
    case contracts::ResultCode::PolicyDenied:
      return MetricsErrorCode::LabelCardinalityExceeded;
    case contracts::ResultCode::ValidationFieldMissing:
      return MetricsErrorCode::ConfigInvalid;
    case contracts::ResultCode::RuntimeRetryExhausted:
      return MetricsErrorCode::QueueFull;
    default:
      return fallback;
  }
}

[[nodiscard]] ToolMetricsEmitResult make_suppressed_result(std::string state_ref) {
  return ToolMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = false,
      .signal_suppressed = true,
      .status = MetricsOperationStatus::success(std::move(state_ref)),
      .metrics_error_code = std::nullopt,
  };
}

}  // namespace

ToolMetricsBridge::ToolMetricsBridge(
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
    ToolMetricsBridgeOptions options)
    : metrics_provider_(std::move(metrics_provider)), options_(std::move(options)) {
  options_.profile_id = normalize_profile_id(std::move(options_.profile_id));
  options_.metrics_granularity =
      normalize_granularity(std::move(options_.metrics_granularity));
  options_.meter_scope_name = normalized_or(std::move(options_.meter_scope_name),
                                            kToolMetricsMeterScopeName);
  options_.meter_scope_version = normalized_or(std::move(options_.meter_scope_version),
                                               kToolMetricsMeterScopeVersion);
}

void ToolMetricsBridge::set_metrics_provider(
    std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider,
    std::string profile_id) {
  metrics_provider_ = std::move(metrics_provider);
  if (!profile_id.empty()) {
    options_.profile_id = normalize_profile_id(std::move(profile_id));
  }

  meter_.reset();
  instrument_handles_.fill(std::nullopt);
  instruments_registered_ = false;
  degraded_ = false;
  last_metrics_error_code_.reset();
}

ToolMetricsEmitResult ToolMetricsBridge::record_preflight_failure(
    const contracts::ToolRequest& request,
    const std::optional<contracts::ToolDescriptor>& descriptor,
    const ToolInvocationEnvelope& envelope,
    const ToolInvocationContext& context) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://tools/disabled");
  }

  return emit_signal(ToolMetricSignal{
      .kind = ToolMetricKind::request_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"request",
                           route_kind_token(envelope.route_facts.has_value()
                                                ? envelope.route_facts->route_kind
                                                : std::optional<std::string>{}),
                           tool_category_token(descriptor),
                           tool_name_token(request, descriptor)}),
      .profile = profile_id_for(context, options_.profile_id),
      .outcome = outcome_for_failure(envelope),
      .error_code = error_code_label_from_envelope(envelope),
  });
}

ToolMetricsEmitResult ToolMetricsBridge::record_admission_denied(
    const contracts::ToolRequest& request,
    const std::optional<contracts::ToolDescriptor>& descriptor,
    std::string_view reason_code,
    const ToolInvocationContext& context) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://tools/disabled");
  }

  return emit_signal(ToolMetricSignal{
      .kind = ToolMetricKind::admission_denied_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"admission",
                           "denied",
                           tool_name_token(request, descriptor),
                           sanitize_stage_token(reason_code)}),
      .profile = profile_id_for(context, options_.profile_id),
      .outcome = "rejected",
      .error_code = sanitize_stage_token(reason_code),
  });
}

ToolMetricsEmitResult ToolMetricsBridge::record_route_selection(
    const contracts::ToolRequest& request,
    const std::optional<contracts::ToolDescriptor>& descriptor,
    const route::ToolRouteDecision& route_decision,
    const ToolInvocationContext& context) {
  static_cast<void>(request);
  static_cast<void>(descriptor);
  if (!is_enabled()) {
    return make_suppressed_result("metrics://tools/disabled");
  }

  if (!route_decision.uses_stale_snapshot) {
    return make_suppressed_result("metrics://tools/route_fresh");
  }

  return emit_signal(ToolMetricSignal{
      .kind = ToolMetricKind::mcp_stale_snapshot_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"route",
                           "mcp_stale",
                           sanitize_stage_token(route_decision.server_id.value_or(
                               std::string("unknown")))}),
      .profile = profile_id_for(context, options_.profile_id),
      .outcome = "degraded",
      .error_code = std::string(kToolMetricNoErrorCodeLabel),
  });
}

ToolMetricsEmitResult ToolMetricsBridge::record_execution_terminal(
    const contracts::ToolRequest& request,
    const std::optional<contracts::ToolDescriptor>& descriptor,
    const ToolInvocationEnvelope& envelope,
    const ToolInvocationContext& context) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://tools/disabled");
  }

  const auto route_kind = route_kind_token(envelope.route_facts.has_value()
                                               ? envelope.route_facts->route_kind
                                               : std::optional<std::string>{});
  const auto tool_name = tool_name_token(request, descriptor);
  const auto outcome = outcome_for_terminal(envelope);
  const auto error_code = error_code_label_from_envelope(envelope);
  auto emission = emit_signal(ToolMetricSignal{
      .kind = ToolMetricKind::request_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"request", route_kind, tool_category_token(descriptor), tool_name}),
      .profile = profile_id_for(context, options_.profile_id),
      .outcome = outcome,
      .error_code = error_code,
  });
  if (!emission.status.ok) {
    return emission;
  }

  if (envelope.tool_result.has_value() &&
      envelope.tool_result->duration_ms.has_value()) {
    emission = emit_signal(ToolMetricSignal{
        .kind = ToolMetricKind::execution_latency_ms,
        .value = static_cast<double>(*envelope.tool_result->duration_ms),
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"execution", route_kind, tool_name}),
        .profile = profile_id_for(context, options_.profile_id),
        .outcome = outcome,
        .error_code = error_code,
    });
    if (!emission.status.ok) {
      return emission;
    }
  }

  if (envelope.tool_result.has_value() &&
      envelope.tool_result->side_effects.has_value() &&
      !envelope.tool_result->success.value_or(true) &&
      !envelope.tool_result->side_effects->empty()) {
    emission = emit_signal(ToolMetricSignal{
        .kind = ToolMetricKind::partial_side_effect_total,
        .value = 1.0,
        .ts_unix_ms = current_time_unix_ms(),
        .stage = join_stage({"execution", "partial_side_effect", route_kind, tool_name}),
        .profile = profile_id_for(context, options_.profile_id),
        .outcome = "degraded",
        .error_code = error_code,
    });
  }

  return emission;
}

ToolMetricsEmitResult ToolMetricsBridge::record_workflow_step_failure(
    std::string_view workflow_id,
    std::string_view step_id,
    std::string_view failure_type,
    const ToolInvocationContext& context) {
  if (!is_enabled()) {
    return make_suppressed_result("metrics://tools/disabled");
  }

  return emit_signal(ToolMetricSignal{
      .kind = ToolMetricKind::workflow_step_failure_total,
      .value = 1.0,
      .ts_unix_ms = current_time_unix_ms(),
      .stage = join_stage({"workflow",
                           sanitize_stage_token(workflow_id),
                           sanitize_stage_token(step_id)}),
      .profile = profile_id_for(context, options_.profile_id),
      .outcome = "failure",
      .error_code = sanitize_stage_token(failure_type),
  });
}

bool ToolMetricsBridge::is_enabled() const {
  return options_.enabled;
}

bool ToolMetricsBridge::granularity_allows(ToolMetricKind kind) const {
  switch (parse_granularity(options_.metrics_granularity)) {
    case ToolMetricsGranularity::full:
      return true;
    case ToolMetricsGranularity::partial:
      return kind == ToolMetricKind::request_total ||
             kind == ToolMetricKind::admission_denied_total ||
             kind == ToolMetricKind::execution_latency_ms ||
             kind == ToolMetricKind::mcp_stale_snapshot_total;
    case ToolMetricsGranularity::minimal:
      return kind == ToolMetricKind::request_total ||
             kind == ToolMetricKind::admission_denied_total;
  }

  return true;
}

std::int64_t ToolMetricsBridge::current_time_unix_ms() const {
  if (options_.now_ms) {
    return options_.now_ms();
  }

  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

ToolMetricsEmitResult ToolMetricsBridge::emit_signal(
    const ToolMetricSignal& signal) {
  ++emission_attempt_total_;

  if (!granularity_allows(signal.kind)) {
    return make_suppressed_result("metrics://tools/granularity_suppressed");
  }

  ToolMetricsEmitResult failure;
  if (!ensure_meter_ready(&failure) || !ensure_instruments_registered(&failure)) {
    return failure;
  }

  const auto handle = instrument_handles_[to_index(signal.kind)];
  if (!handle.has_value() || !handle->is_valid()) {
    return make_failure_result(
        MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(MetricsErrorCode::ProviderNotReady,
                                    "tool metrics bridge requires valid instruments before recording samples"));
  }

  MetricSample sample{
      .identity_ref = make_metric_identity(signal.kind),
      .value = signal.value,
      .ts_unix_ms = signal.ts_unix_ms,
      .labels = MetricLabels{
          .module = "tools",
          .stage = signal.stage,
          .profile = signal.profile,
          .outcome = signal.outcome,
          .error_code = signal.error_code,
      },
  };
  if (!sample.is_valid()) {
    return make_failure_result(
        MetricsErrorCode::ConfigInvalid,
        make_metrics_failure_status(MetricsErrorCode::ConfigInvalid,
                                    "tool metrics bridge produced an invalid metric sample"));
  }

  const auto status = meter_->record(sample);
  if (!status.ok) {
    const auto error_code = infer_metrics_error_code(status, MetricsErrorCode::ExportFailure);
    return make_failure_result(error_code, status);
  }

  return ToolMetricsEmitResult{
      .emitted = true,
      .bridge_degraded = false,
      .signal_suppressed = false,
      .status = status,
      .metrics_error_code = std::nullopt,
  };
}

bool ToolMetricsBridge::ensure_meter_ready(ToolMetricsEmitResult* failure) {
  if (meter_) {
    return true;
  }

  if (!metrics_provider_) {
    *failure = make_failure_result(
        MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(MetricsErrorCode::ProviderNotReady,
                                    "tool metrics bridge requires an IMetricsProvider before emitting signals"));
    return false;
  }

  meter_ = metrics_provider_->get_meter(infra::metrics::MeterScope{
      .name = options_.meter_scope_name,
      .version = options_.meter_scope_version,
      .schema_url = {},
  });
  if (!meter_) {
    *failure = make_failure_result(
        MetricsErrorCode::ProviderNotReady,
        make_metrics_failure_status(MetricsErrorCode::ProviderNotReady,
                                    "tool metrics bridge could not acquire the tools meter scope"));
    return false;
  }

  return true;
}

bool ToolMetricsBridge::ensure_instruments_registered(ToolMetricsEmitResult* failure) {
  if (instruments_registered_) {
    return true;
  }

  for (const auto kind : kToolMetricKinds) {
    const auto identity = make_metric_identity(kind);
    std::optional<infra::metrics::InstrumentHandle> handle;
    if (metric_type(kind) == MetricType::Histogram) {
      handle = meter_->create_histogram(identity);
    } else {
      handle = meter_->create_counter(identity);
    }

    if (!handle.has_value() || !handle->is_valid()) {
      *failure = make_failure_result(
          MetricsErrorCode::ProviderNotReady,
          make_metrics_failure_status(MetricsErrorCode::ProviderNotReady,
                                      "tool metrics bridge could not register the frozen metric families"));
      return false;
    }

    instrument_handles_[to_index(kind)] = std::move(handle);
  }

  instruments_registered_ = true;
  return true;
}

ToolMetricsEmitResult ToolMetricsBridge::make_failure_result(
    MetricsErrorCode error_code,
    MetricsOperationStatus status) {
  degraded_ = degraded_ || metrics_error_causes_degraded(error_code);
  ++emission_failure_total_;
  last_metrics_error_code_ = error_code;

  return ToolMetricsEmitResult{
      .emitted = false,
      .bridge_degraded = degraded_,
      .signal_suppressed = false,
      .status = std::move(status),
      .metrics_error_code = error_code,
  };
}

}  // namespace dasall::tools::ops