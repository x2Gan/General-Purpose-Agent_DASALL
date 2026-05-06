#include "bridges/ServiceTraceBridge.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value,
                                        std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] std::string hex_encode(std::uint64_t value, std::size_t width) {
  std::ostringstream builder;
  builder << std::hex << std::nouppercase << std::setfill('0')
          << std::setw(static_cast<int>(width)) << value;
  auto encoded = builder.str();
  if (encoded.size() > width) {
    encoded = encoded.substr(encoded.size() - width);
  }
  if (encoded.size() < width) {
    encoded.insert(encoded.begin(), width - encoded.size(), '0');
  }
  if (std::all_of(encoded.begin(), encoded.end(), [](const char ch) {
        return ch == '0';
      })) {
    encoded.back() = '1';
  }
  return encoded;
}

[[nodiscard]] std::uint64_t fnv1a(std::string_view value, std::uint64_t seed) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash = 1469598103934665603ULL ^ seed;
  for (const char ch : value) {
    hash ^= static_cast<unsigned char>(ch);
    hash *= kPrime;
  }
  return hash;
}

[[nodiscard]] std::string normalized_hex_id(std::string_view value,
                                            std::size_t width,
                                            std::uint64_t salt) {
  if (infra::tracing::is_lower_hex_string(value, width)) {
    return std::string(value);
  }

  const auto high = hex_encode(fnv1a(value, salt), width >= 16U ? 16U : width);
  if (width <= 16U) {
    return high;
  }

  return high +
         hex_encode(fnv1a(value, salt ^ 0x9e3779b97f4a7c15ULL), width - 16U);
}

[[nodiscard]] std::string trace_detail_ref(std::string_view segment) {
  return std::string("trace://services/") + std::string(segment);
}

[[nodiscard]] infra::tracing::TraceAttributeMap make_context_attrs(
    const ServiceCallContext& context,
    const ServiceTraceBridgeOptions& options) {
  infra::tracing::TraceAttributeMap attrs;
  attrs.emplace("services.request_id", context.request_id);
  attrs.emplace("services.session_id", context.session_id);
  attrs.emplace("services.trace_ref", context.trace_id);
  attrs.emplace("services.tool_call_id", context.tool_call_id);
  attrs.emplace("services.goal_id", context.goal_id);
  attrs.emplace("services.profile", options.profile_id);
  attrs.emplace("services.trace_sample_ratio", options.trace_sample_ratio);
  attrs.emplace("services.deadline_ms",
                static_cast<std::int64_t>(context.deadline_ms));
  if (context.budget_guard.has_value() &&
      context.budget_guard->max_latency_ms.has_value()) {
    attrs.emplace("services.budget_ms",
                  static_cast<std::int64_t>(
                      *context.budget_guard->max_latency_ms));
  }
  return attrs;
}

[[nodiscard]] infra::tracing::SpanDescriptor make_facade_descriptor(
    const ServiceCallContext& context,
    const ServiceTraceBridgeOptions& options,
    std::string_view operation) {
  auto attrs = make_context_attrs(context, options);
  attrs.emplace("services.stage", std::string("facade"));
  attrs.emplace("services.operation", std::string(operation));
  return infra::tracing::SpanDescriptor{
      .name = std::string("services.facade.") + std::string(operation),
      .kind = infra::tracing::SpanKind::Server,
      .start_ts_unix_ms = current_time_unix_ms(),
      .attrs = std::move(attrs),
      .links = {},
  };
}

[[nodiscard]] infra::tracing::SpanDescriptor make_lane_descriptor(
    const ServiceCallContext& context,
    const ServiceTraceBridgeOptions& options,
    std::string_view lane_name,
    std::string_view operation,
    const CapabilityTargetRef* target) {
  auto attrs = make_context_attrs(context, options);
  attrs.emplace("services.stage", std::string("lane"));
  attrs.emplace("services.lane", std::string(lane_name));
  attrs.emplace("services.operation", std::string(operation));
  if (target != nullptr) {
    attrs.emplace("services.capability_id", target->capability_id);
    attrs.emplace("services.target_id", target->target_id);
  }
  return infra::tracing::SpanDescriptor{
      .name = std::string("services.lane.") + std::string(lane_name) + "." +
              std::string(operation),
      .kind = infra::tracing::SpanKind::Internal,
      .start_ts_unix_ms = current_time_unix_ms(),
      .attrs = std::move(attrs),
      .links = {},
  };
}

[[nodiscard]] std::string request_kind_name(AdapterRouteRequestKind kind) {
  return kind == AdapterRouteRequestKind::action ? "action" : "query";
}

[[nodiscard]] infra::tracing::SpanDescriptor make_adapter_descriptor(
    const ServiceTraceBridgeOptions& options,
    const AdapterSelection& selection,
    const AdapterInvocationRequest& request) {
  infra::tracing::TraceAttributeMap attrs;
  attrs.emplace("services.stage", std::string("adapter"));
  attrs.emplace("services.profile", options.profile_id);
  attrs.emplace("services.adapter_id", selection.adapter_id);
  attrs.emplace("services.route_kind",
                std::string(route_kind_name(selection.route_kind)));
  attrs.emplace("services.capability_id", request.capability_id);
  attrs.emplace("services.target_id", request.target_id);
  attrs.emplace("services.operation", request.operation_name);
  attrs.emplace("services.request_kind", request_kind_name(request.request_kind));
  return infra::tracing::SpanDescriptor{
      .name = std::string("services.adapter.") +
              std::string(route_kind_name(selection.route_kind)) + "." +
              selection.adapter_id,
      .kind = infra::tracing::SpanKind::Client,
      .start_ts_unix_ms = current_time_unix_ms(),
      .attrs = std::move(attrs),
      .links = {},
  };
}

[[nodiscard]] infra::tracing::SpanDescriptor make_external_descriptor(
    const ServiceTraceBridgeOptions& options,
    const AdapterSelection& selection,
    const AdapterInvocationRequest& request) {
  infra::tracing::TraceAttributeMap attrs;
  attrs.emplace("services.stage", std::string("external"));
  attrs.emplace("services.profile", options.profile_id);
  attrs.emplace("services.adapter_id", selection.adapter_id);
  attrs.emplace("services.capability_id", request.capability_id);
  attrs.emplace("services.target_id", request.target_id);
  attrs.emplace("services.operation", request.operation_name);
  attrs.emplace("services.route_kind",
                std::string(route_kind_name(selection.route_kind)));
  return infra::tracing::SpanDescriptor{
      .name = std::string("services.external.") + request.capability_id + "." +
              request.operation_name,
      .kind = infra::tracing::SpanKind::Client,
      .start_ts_unix_ms = current_time_unix_ms(),
      .attrs = std::move(attrs),
      .links = {},
  };
}

[[nodiscard]] contracts::ResultCode adapter_result_code_for(
    AdapterTransportOutcome outcome) {
  switch (outcome) {
    case AdapterTransportOutcome::timeout:
    case AdapterTransportOutcome::unreachable:
      return contracts::ResultCode::ProviderTimeout;
    case AdapterTransportOutcome::partial:
    case AdapterTransportOutcome::rejected:
      return contracts::ResultCode::ToolExecutionFailed;
    case AdapterTransportOutcome::acknowledged:
      return contracts::ResultCode::ToolExecutionFailed;
  }

  return contracts::ResultCode::ToolExecutionFailed;
}

[[nodiscard]] contracts::ResultCode effective_failure_code_for(
    const std::optional<contracts::ResultCode>& result_code,
    const std::optional<contracts::ErrorInfo>& error) {
  const auto effective_code = service_result_effective_failure_code(result_code, error);
  return effective_code.value_or(contracts::ResultCode::ToolExecutionFailed);
}

[[nodiscard]] std::string receipt_error_message(const AdapterReceipt& receipt) {
  if (!receipt.provider_status_code.empty()) {
    return receipt.provider_status_code;
  }
  return std::string(transport_outcome_name(receipt.transport_outcome));
}

}  // namespace

bool ServiceTraceBridgeStatus::has_consistent_state() const {
  if (detail_ref.empty()) {
    return false;
  }

  if (last_error_code.has_value() &&
      contracts::classify_result_code(*last_error_code) ==
          contracts::ResultCodeCategory::Unknown) {
    return false;
  }

  return true;
}

ServiceTraceBridge::ServiceTraceBridge(
    std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider,
    ServiceTraceBridgeOptions options)
    : tracer_provider_(std::move(tracer_provider)), options_(std::move(options)) {
  options_.profile_id = normalized_or(std::move(options_.profile_id), "unknown");
  options_.tracer_scope_name = normalized_or(
      std::move(options_.tracer_scope_name), kServiceTraceDefaultScopeName);
  options_.tracer_scope_version = normalized_or(
      std::move(options_.tracer_scope_version), kServiceTraceDefaultScopeVersion);
  options_.schema_url = normalized_or(std::move(options_.schema_url),
                                      kServiceTraceDefaultSchemaUrl);
}

void ServiceTraceBridge::set_tracer_provider(
    std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider,
    std::string profile_id) {
  tracer_provider_ = std::move(tracer_provider);
  tracer_.reset();
  if (!profile_id.empty()) {
    options_.profile_id = normalized_or(std::move(profile_id), "unknown");
  }
  degraded_ = false;
  last_error_code_.reset();
  last_detail_ref_ = trace_detail_ref("idle");
}

bool ServiceTraceBridge::has_active_tracer() const {
  return static_cast<bool>(tracer_);
}

bool ServiceTraceBridge::is_degraded() const {
  return degraded_;
}

ServiceTraceSpan ServiceTraceBridge::start_facade_span(
    const ServiceCallContext& context,
    std::string_view operation) {
  auto parent = make_tool_parent_context(context);
  const auto detail_ref = trace_detail_ref(
      std::string("facade/") + std::string(operation) + "/" +
      normalized_or(context.request_id, "unknown"));
  const auto* parent_ptr = parent.state == infra::tracing::TraceContextState::Active
                               ? &parent
                               : nullptr;
  return start_span(make_facade_descriptor(context, options_, operation),
                    detail_ref,
                    parent_ptr);
}

ServiceTraceSpan ServiceTraceBridge::start_lane_span(
    const ServiceCallContext& context,
    std::string_view lane_name,
    std::string_view operation,
    const CapabilityTargetRef* target) {
  const auto detail_ref = trace_detail_ref(
      std::string("lane/") + std::string(lane_name) + "/" +
      std::string(operation) + "/" + normalized_or(context.request_id, "unknown"));
  return start_span(make_lane_descriptor(context, options_, lane_name, operation, target),
                    detail_ref);
}

ServiceTraceSpan ServiceTraceBridge::start_adapter_span(
    const AdapterSelection& selection,
    const AdapterInvocationRequest& request) {
  const auto detail_ref = trace_detail_ref(
      std::string("adapter/") + selection.adapter_id + "/" +
      normalized_or(request.request_id, "unknown"));
  return start_span(make_adapter_descriptor(options_, selection, request), detail_ref);
}

ServiceTraceSpan ServiceTraceBridge::start_external_span(
    const AdapterSelection& selection,
    const AdapterInvocationRequest& request) {
  const auto detail_ref = trace_detail_ref(
      std::string("external/") + request.capability_id + "/" + request.target_id +
      "/" + request.operation_name);
  return start_span(make_external_descriptor(options_, selection, request), detail_ref);
}

void ServiceTraceBridge::complete_span(ServiceTraceSpan* scope,
                                       const ExecutionCommandResult& result) {
  if (result.error.has_value()) {
    mark_error(scope,
               effective_failure_code_for(result.code, result.error),
               result.error->details.message,
               result.error->details.stage.empty() ? std::string("execution_command_lane")
                                                   : result.error->details.stage);
    return;
  }

  mark_success(scope);
}

void ServiceTraceBridge::complete_span(ServiceTraceSpan* scope,
                                       const ExecutionQueryResult& result) {
  if (result.error.has_value()) {
    mark_error(scope,
               effective_failure_code_for(result.code, result.error),
               result.error->details.message,
               result.error->details.stage.empty() ? std::string("execution_query_lane")
                                                   : result.error->details.stage);
    return;
  }

  mark_success(scope);
}

void ServiceTraceBridge::complete_span(ServiceTraceSpan* scope,
                                       const ExecutionDiagnoseResult& result) {
  if (result.error.has_value()) {
    mark_error(scope,
               effective_failure_code_for(result.code, result.error),
               result.error->details.message,
               result.error->details.stage.empty() ? std::string("execution_diagnose_service")
                                                   : result.error->details.stage);
    return;
  }

  mark_success(scope);
}

void ServiceTraceBridge::complete_span(ServiceTraceSpan* scope,
                                       const DataQueryResult& result) {
  if (scope != nullptr && scope->is_valid()) {
    scope->span->set_attribute("services.from_cache",
                               infra::tracing::TraceAttributeValue{result.from_cache});
  }
  if (result.error.has_value()) {
    mark_error(scope,
               effective_failure_code_for(result.code, result.error),
               result.error->details.message,
               result.error->details.stage.empty() ? std::string("data_query_lane")
                                                   : result.error->details.stage);
    return;
  }

  mark_success(scope);
}

void ServiceTraceBridge::complete_span(ServiceTraceSpan* scope,
                                       const DataCatalogResult& result) {
  if (result.error.has_value()) {
    mark_error(scope,
               effective_failure_code_for(result.code, result.error),
               result.error->details.message,
               result.error->details.stage.empty() ? std::string("data_query_lane")
                                                   : result.error->details.stage);
    return;
  }

  mark_success(scope);
}

void ServiceTraceBridge::complete_span(ServiceTraceSpan* scope,
                                       const AdapterReceipt& receipt) {
  if (scope != nullptr && scope->is_valid()) {
    scope->span->set_attribute(
        "services.latency_ms",
        infra::tracing::TraceAttributeValue{static_cast<std::int64_t>(receipt.latency_ms)});
    scope->span->set_attribute(
        "services.transport_outcome",
        infra::tracing::TraceAttributeValue{
            std::string(transport_outcome_name(receipt.transport_outcome))});
  }

  if (receipt.transport_outcome == AdapterTransportOutcome::acknowledged) {
    mark_success(scope);
    return;
  }

  mark_error(scope,
             adapter_result_code_for(receipt.transport_outcome),
             receipt_error_message(receipt),
             std::string("adapter_bridge"));
}

void ServiceTraceBridge::mark_success(ServiceTraceSpan* scope) {
  safe_finish_span(scope,
                   infra::tracing::SpanStatusCode::Ok,
                   {},
                   contracts::ResultCode::ToolExecutionFailed);
}

void ServiceTraceBridge::mark_error(ServiceTraceSpan* scope,
                                    contracts::ResultCode result_code,
                                    std::string message,
                                    std::string) {
  safe_finish_span(scope,
                   infra::tracing::SpanStatusCode::Error,
                   std::move(message),
                   result_code);
}

ServiceTraceBridgeStatus ServiceTraceBridge::get_status() const {
  return ServiceTraceBridgeStatus{
      .started_span_total = started_span_total_,
      .span_failure_total = span_failure_total_,
      .degraded = degraded_,
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_,
  };
}

bool ServiceTraceBridge::is_enabled() const {
  return options_.enabled;
}

infra::tracing::TracerScope ServiceTraceBridge::make_scope() const {
  return infra::tracing::TracerScope{
      .name = options_.tracer_scope_name,
      .version = options_.tracer_scope_version,
      .schema_url = options_.schema_url,
  };
}

infra::tracing::TraceContext ServiceTraceBridge::make_tool_parent_context(
    const ServiceCallContext& context) const {
  if (context.trace_id.empty()) {
    return infra::tracing::TraceContext::noop();
  }

  std::string parent_seed = !context.tool_call_id.empty() ? context.tool_call_id
                                                           : context.request_id;
  if (parent_seed.empty()) {
    parent_seed = context.trace_id;
  }

  return infra::tracing::TraceContext{
      .trace_id = normalized_hex_id(context.trace_id,
                                    infra::tracing::kTraceIdHexLength,
                                    0x726f6f742d747261ULL),
      .span_id = normalized_hex_id(parent_seed,
                                   infra::tracing::kSpanIdHexLength,
                                   0x746f6f6c2d737061ULL),
      .trace_flags = 0x01U,
      .trace_state = {},
      .parent_span_id = {},
      .state = infra::tracing::TraceContextState::Active,
      .is_remote = true,
  };
}

ServiceTraceSpan ServiceTraceBridge::start_span(
    infra::tracing::SpanDescriptor descriptor,
    std::string detail_ref,
    const infra::tracing::TraceContext* parent) {
  if (!is_enabled()) {
    return {};
  }

  if (!descriptor.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   detail_ref.empty() ? trace_detail_ref("invalid_descriptor")
                                      : detail_ref);
    return {};
  }

  auto tracer = ensure_tracer();
  if (!tracer) {
    return {};
  }

  try {
    auto span = tracer->start_span(descriptor, parent);
    if (!span) {
      record_failure(contracts::ResultCode::ValidationFieldMissing,
                     detail_ref.empty() ? trace_detail_ref("start_span_failed")
                                        : detail_ref);
      return {};
    }

    ++started_span_total_;
    last_detail_ref_ = detail_ref.empty() ? trace_detail_ref("active") : detail_ref;
    return ServiceTraceSpan{
        .tracer = std::move(tracer),
        .span = std::move(span),
        .detail_ref = std::move(detail_ref),
    };
  } catch (...) {
    record_failure(contracts::ResultCode::ProviderTimeout,
                   detail_ref.empty() ? trace_detail_ref("provider_exception")
                                      : detail_ref);
    return {};
  }
}

std::shared_ptr<infra::tracing::ITracer> ServiceTraceBridge::ensure_tracer() {
  if (tracer_) {
    return tracer_;
  }

  if (!tracer_provider_) {
    record_failure(contracts::ResultCode::ProviderTimeout,
                   trace_detail_ref("provider_missing"));
    return {};
  }

  try {
    tracer_ = tracer_provider_->get_tracer(make_scope());
  } catch (...) {
    tracer_.reset();
  }

  if (!tracer_) {
    record_failure(contracts::ResultCode::ProviderTimeout,
                   trace_detail_ref("tracer_unavailable"));
    return {};
  }

  return tracer_;
}

void ServiceTraceBridge::safe_finish_span(ServiceTraceSpan* scope,
                                          infra::tracing::SpanStatusCode status_code,
                                          std::string message,
                                          contracts::ResultCode result_code) {
  if (scope == nullptr || !scope->is_valid()) {
    return;
  }

  const auto detail_ref = scope->detail_ref.empty() ? trace_detail_ref("unknown")
                                                    : scope->detail_ref;
  try {
    scope->span->set_status(status_code, message);
    const auto end_result = scope->span->end(current_time_unix_ms());
    if (!end_result.is_valid()) {
      record_failure(result_code, detail_ref);
      return;
    }
    clear_error_state(detail_ref);
  } catch (...) {
    record_failure(result_code, detail_ref);
  }
}

void ServiceTraceBridge::record_failure(contracts::ResultCode result_code,
                                        std::string detail_ref) {
  ++span_failure_total_;
  degraded_ = true;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref.empty() ? trace_detail_ref("failure")
                                        : std::move(detail_ref);
}

void ServiceTraceBridge::clear_error_state(std::string detail_ref) {
  last_detail_ref_ = detail_ref.empty() ? trace_detail_ref("completed")
                                        : std::move(detail_ref);
  if (span_failure_total_ == 0U) {
    degraded_ = false;
    last_error_code_.reset();
  }
}

}  // namespace dasall::services::internal