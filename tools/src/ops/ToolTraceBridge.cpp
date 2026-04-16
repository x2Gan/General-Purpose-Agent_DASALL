#include "ops/ToolTraceBridge.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

#include "RuntimePolicySnapshot.h"

namespace dasall::tools::ops {

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
  return std::string("trace://tools/") + std::string(segment);
}

[[nodiscard]] std::string profile_id_for(const ToolInvocationContext& context,
                                         std::string fallback) {
  if (context.profile_snapshot != nullptr) {
    return normalized_or(context.profile_snapshot->effective_profile_id(), "unknown");
  }

  return normalized_or(std::move(fallback), "unknown");
}

[[nodiscard]] infra::tracing::TraceAttributeMap make_context_attrs(
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context,
    const ToolTraceBridgeOptions& options) {
  infra::tracing::TraceAttributeMap attrs;
  if (request.request_id.has_value()) {
    attrs.emplace("tools.request_id", *request.request_id);
  }
  if (request.tool_call_id.has_value()) {
    attrs.emplace("tools.tool_call_id", *request.tool_call_id);
  }
  if (request.tool_name.has_value()) {
    attrs.emplace("tools.tool_name", *request.tool_name);
  }
  if (context.caller_domain.has_value()) {
    attrs.emplace("tools.caller_domain", *context.caller_domain);
  }
  if (context.session_id.has_value()) {
    attrs.emplace("tools.session_id", *context.session_id);
  }
  if (request.goal_id.has_value()) {
    attrs.emplace("tools.goal_id", *request.goal_id);
  }
  attrs.emplace("tools.profile", profile_id_for(context, options.profile_id));
  attrs.emplace("tools.trace_sample_ratio", options.trace_sample_ratio);
  return attrs;
}

[[nodiscard]] infra::tracing::SpanDescriptor make_root_descriptor(
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context,
    const ToolTraceBridgeOptions& options) {
  auto attrs = make_context_attrs(request, context, options);
  attrs.emplace("tools.stage", std::string("invoke"));
  return infra::tracing::SpanDescriptor{
      .name = std::string("tool.invoke"),
      .kind = infra::tracing::SpanKind::Server,
      .start_ts_unix_ms = current_time_unix_ms(),
      .attrs = std::move(attrs),
      .links = {},
  };
}

[[nodiscard]] infra::tracing::SpanDescriptor make_stage_descriptor(
    std::string_view span_name,
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context,
    const ToolTraceBridgeOptions& options,
    const ToolTraceStageDetails& details) {
  auto attrs = make_context_attrs(request, context, options);
  attrs.emplace("tools.stage", std::string(span_name));
  if (details.route_kind.has_value()) {
    attrs.emplace("tools.route_kind", *details.route_kind);
  }
  if (details.lane_key.has_value()) {
    attrs.emplace("tools.lane_key", *details.lane_key);
  }
  if (details.server_id.has_value()) {
    attrs.emplace("tools.server_id", *details.server_id);
  }
  if (details.reason_code.has_value()) {
    attrs.emplace("tools.reason_code", *details.reason_code);
  }

  return infra::tracing::SpanDescriptor{
      .name = std::string(span_name),
      .kind = infra::tracing::SpanKind::Internal,
      .start_ts_unix_ms = current_time_unix_ms(),
      .attrs = std::move(attrs),
      .links = {},
  };
}

[[nodiscard]] std::string detail_segment_for_request(
    std::string_view stage,
    const contracts::ToolRequest& request) {
  const auto request_key = request.tool_call_id.value_or(
      request.request_id.value_or(std::string("unknown")));
  return std::string(stage) + "/" + normalized_or(request_key, "unknown");
}

}  // namespace

bool ToolTraceBridgeStatus::has_consistent_state() const {
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

ToolTraceBridge::ToolTraceBridge(
    std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider,
    ToolTraceBridgeOptions options)
    : tracer_provider_(std::move(tracer_provider)), options_(std::move(options)) {
  options_.profile_id = normalized_or(std::move(options_.profile_id), "unknown");
  options_.tracer_scope_name = normalized_or(
      std::move(options_.tracer_scope_name), kToolTraceDefaultScopeName);
  options_.tracer_scope_version = normalized_or(
      std::move(options_.tracer_scope_version), kToolTraceDefaultScopeVersion);
  options_.schema_url =
      normalized_or(std::move(options_.schema_url), kToolTraceDefaultSchemaUrl);
}

void ToolTraceBridge::set_tracer_provider(
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

bool ToolTraceBridge::has_active_tracer() const {
  return static_cast<bool>(tracer_);
}

bool ToolTraceBridge::is_degraded() const {
  return degraded_;
}

ToolTraceSpan ToolTraceBridge::start_root_span(
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context) {
  auto parent = make_runtime_parent_context(request, context);
  const auto* parent_ptr = parent.state == infra::tracing::TraceContextState::Active
                               ? &parent
                               : nullptr;
  return start_span(make_root_descriptor(request, context, options_),
                    trace_detail_ref(detail_segment_for_request("invoke", request)),
                    parent_ptr);
}

ToolTraceSpan ToolTraceBridge::start_stage_span(
    std::string_view span_name,
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context,
    ToolTraceStageDetails details) {
  return start_span(
      make_stage_descriptor(span_name, request, context, options_, details),
      trace_detail_ref(detail_segment_for_request(span_name, request)));
}

void ToolTraceBridge::mark_success(ToolTraceSpan* scope) {
  safe_finish_span(scope,
                   infra::tracing::SpanStatusCode::Ok,
                   {},
                   contracts::ResultCode::ToolExecutionFailed);
}

void ToolTraceBridge::mark_error(ToolTraceSpan* scope,
                                 contracts::ResultCode result_code,
                                 std::string message,
                                 std::string stage) {
  if (scope != nullptr && scope->is_valid()) {
    scope->span->set_attribute("tools.error_stage",
                               infra::tracing::TraceAttributeValue{stage});
  }
  safe_finish_span(scope,
                   infra::tracing::SpanStatusCode::Error,
                   std::move(message),
                   result_code);
}

ToolTraceBridgeStatus ToolTraceBridge::get_status() const {
  return ToolTraceBridgeStatus{
      .started_span_total = started_span_total_,
      .span_failure_total = span_failure_total_,
      .degraded = degraded_,
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_,
  };
}

bool ToolTraceBridge::is_enabled() const {
  return options_.enabled;
}

infra::tracing::TracerScope ToolTraceBridge::make_scope() const {
  return infra::tracing::TracerScope{
      .name = options_.tracer_scope_name,
      .version = options_.tracer_scope_version,
      .schema_url = options_.schema_url,
  };
}

infra::tracing::TraceContext ToolTraceBridge::make_runtime_parent_context(
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context) const {
  if (!context.trace.trace_id.has_value() || context.trace.trace_id->empty()) {
    return infra::tracing::TraceContext::noop();
  }

  std::string parent_seed;
  if (context.trace.span_id.has_value() && !context.trace.span_id->empty()) {
    parent_seed = *context.trace.span_id;
  } else if (context.trace.parent_span_id.has_value() &&
             !context.trace.parent_span_id->empty()) {
    parent_seed = *context.trace.parent_span_id;
  } else if (request.tool_call_id.has_value() && !request.tool_call_id->empty()) {
    parent_seed = *request.tool_call_id;
  } else if (request.request_id.has_value() && !request.request_id->empty()) {
    parent_seed = *request.request_id;
  } else {
    parent_seed = *context.trace.trace_id;
  }

  return infra::tracing::TraceContext{
      .trace_id = normalized_hex_id(*context.trace.trace_id,
                                    infra::tracing::kTraceIdHexLength,
                                    0x746f6f6c2d747261ULL),
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

ToolTraceSpan ToolTraceBridge::start_span(
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
    return ToolTraceSpan{
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

std::shared_ptr<infra::tracing::ITracer> ToolTraceBridge::ensure_tracer() {
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

void ToolTraceBridge::safe_finish_span(ToolTraceSpan* scope,
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

void ToolTraceBridge::record_failure(contracts::ResultCode result_code,
                                     std::string detail_ref) {
  ++span_failure_total_;
  degraded_ = true;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref.empty() ? trace_detail_ref("failure")
                                        : std::move(detail_ref);
}

void ToolTraceBridge::clear_error_state(std::string detail_ref) {
  last_detail_ref_ = detail_ref.empty() ? trace_detail_ref("completed")
                                        : std::move(detail_ref);
  if (span_failure_total_ == 0U) {
    degraded_ = false;
    last_error_code_.reset();
  }
}

}  // namespace dasall::tools::ops