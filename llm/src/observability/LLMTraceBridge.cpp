#include "LLMTraceBridge.h"

#include <cstddef>
#include <string>
#include <utility>

#include "tracing/ISpan.h"

namespace dasall::llm::observability {
namespace {

constexpr std::string_view kLLMTraceBridgeStage = "llm.observability.trace";
constexpr std::string_view kLLMTraceBridgeSourceRef = "LLMTraceBridge";

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

[[nodiscard]] infra::tracing::TraceOperationStatus make_trace_failure_status(
    infra::tracing::TraceErrorCode error_code,
    std::string message) {
  const auto mapping = infra::tracing::map_trace_error_code(error_code);
  return infra::tracing::TraceOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::string(kLLMTraceBridgeStage),
      std::string(kLLMTraceBridgeSourceRef));
}

[[nodiscard]] infra::tracing::SpanKind span_kind(LLMTraceSpanKind kind) {
  return kind == LLMTraceSpanKind::AdapterInvoke
             ? infra::tracing::SpanKind::Client
             : infra::tracing::SpanKind::Internal;
}

[[nodiscard]] std::string status_message(const LLMTraceSpanSignal& signal) {
  if (!signal.error_type.empty()) {
    return signal.error_type;
  }

  if (!signal.failure_category.empty()) {
    return signal.failure_category;
  }

  return signal.outcome;
}

}  // namespace

bool LLMTraceSpanSignal::has_consistent_values() const {
  if (request_id.empty() || llm_call_id.empty() || stage.empty() ||
      resolved_route.empty() || model_name.empty() || prompt_id.empty() ||
      prompt_version.empty() || reasoning_mode_requested.empty() ||
      reasoning_mode_effective.empty() || selection_reason_codes.empty() ||
      completed_at_ms <= 0 || !is_llm_trace_outcome(outcome) ||
      !std::isfinite(actual_cost_estimate_usd) || actual_cost_estimate_usd < 0.0) {
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

  if (parent_context.has_value() && !parent_context->is_valid()) {
    return false;
  }

  return true;
}

bool LLMTraceBridgeStatus::is_valid() const {
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

LLMTraceBridge::LLMTraceBridge(std::shared_ptr<infra::tracing::ITracer> tracer)
    : tracer_(std::move(tracer)) {}

void LLMTraceBridge::set_tracer(std::shared_ptr<infra::tracing::ITracer> tracer) {
  tracer_ = std::move(tracer);
}

LLMTraceWriteResult LLMTraceBridge::record_span(const LLMTraceSpanSignal& signal) {
  const std::string detail_ref = signal.detail_ref.empty()
                                     ? std::string(kLLMTraceDefaultDetailRef)
                                     : signal.detail_ref;
  if (!signal.has_consistent_values()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return LLMTraceWriteResult{
        .emitted = false,
        .status = make_trace_failure_status(
            infra::tracing::TraceErrorCode::ConfigInvalid,
            "llm trace bridge requires a valid span signal before emitting telemetry"),
        .trace_error_code = infra::tracing::TraceErrorCode::ConfigInvalid,
    };
  }

  if (!tracer_) {
    record_failure(contracts::ResultCode::ProviderTimeout, detail_ref);
    return LLMTraceWriteResult{
        .emitted = false,
        .status = make_trace_failure_status(
            infra::tracing::TraceErrorCode::ProviderNotReady,
            "llm trace bridge requires an ITracer sink before span emission"),
        .trace_error_code = infra::tracing::TraceErrorCode::ProviderNotReady,
    };
  }

  auto descriptor = make_descriptor(signal);
  if (!descriptor.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return LLMTraceWriteResult{
        .emitted = false,
        .status = make_trace_failure_status(
            infra::tracing::TraceErrorCode::ConfigInvalid,
            "llm trace bridge produced an invalid span descriptor"),
        .trace_error_code = infra::tracing::TraceErrorCode::ConfigInvalid,
    };
  }

  const infra::tracing::TraceContext* parent = nullptr;
  if (signal.parent_context.has_value()) {
    parent = &*signal.parent_context;
  }

  auto span = tracer_->start_span(descriptor, parent);
  if (!span) {
    record_failure(contracts::ResultCode::ProviderTimeout, detail_ref);
    return LLMTraceWriteResult{
        .emitted = false,
        .status = make_trace_failure_status(
            infra::tracing::TraceErrorCode::ProviderNotReady,
            "llm trace bridge did not receive an active span from the tracer"),
        .trace_error_code = infra::tracing::TraceErrorCode::ProviderNotReady,
    };
  }

  span->add_event("llm.selection",
                  infra::tracing::TraceAttributeMap{
                      {"reason_codes", join_values(signal.selection_reason_codes, ",")},
                      {"fallback_used", signal.fallback_used},
                  });

  if (signal.outcome == "failure" || signal.outcome == "rejected") {
    span->set_status(infra::tracing::SpanStatusCode::Error,
                     status_message(signal));
  }

  const auto end_result = span->end(signal.completed_at_ms);
  if (!end_result.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return LLMTraceWriteResult{
        .emitted = false,
        .status = make_trace_failure_status(
            infra::tracing::TraceErrorCode::ConfigInvalid,
            "llm trace bridge received an invalid span end result"),
        .trace_error_code = infra::tracing::TraceErrorCode::ConfigInvalid,
    };
  }

  record_success(detail_ref);
  return LLMTraceWriteResult{
      .emitted = true,
      .status = infra::tracing::TraceOperationStatus::success(
          "llm-observability://trace"),
      .trace_error_code = std::nullopt,
  };
}

LLMTraceBridgeStatus LLMTraceBridge::get_status() const {
  return LLMTraceBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0U || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? std::string(kLLMTraceDefaultDetailRef)
                                             : last_detail_ref_,
  };
}

infra::tracing::SpanDescriptor LLMTraceBridge::make_descriptor(
    const LLMTraceSpanSignal& signal) const {
  infra::tracing::TraceAttributeMap attrs;
  attrs.emplace("request_id", signal.request_id);
  attrs.emplace("llm_call_id", signal.llm_call_id);
  attrs.emplace("request_mode", signal.request_mode);
  attrs.emplace("stage", signal.stage);
  attrs.emplace("resolved_route", signal.resolved_route);
  attrs.emplace("model_name", signal.model_name);
  attrs.emplace("prompt_id", signal.prompt_id);
  attrs.emplace("prompt_version", signal.prompt_version);
  attrs.emplace("fallback_used", signal.fallback_used);
  attrs.emplace("latency_ms", static_cast<std::uint64_t>(signal.latency_ms));
  attrs.emplace("failure_category",
                signal.failure_category.empty() ? std::string("none")
                                                : signal.failure_category);
  attrs.emplace("error_type",
                signal.error_type.empty() ? std::string("none")
                                          : signal.error_type);
  attrs.emplace("selection_reason_codes",
                join_values(signal.selection_reason_codes, ","));
  attrs.emplace("estimated_input_tokens",
                static_cast<std::uint64_t>(signal.estimated_input_tokens));
  attrs.emplace("prompt_cache_hit_tokens",
                static_cast<std::uint64_t>(signal.prompt_cache_hit_tokens));
  attrs.emplace("prompt_cache_miss_tokens",
                static_cast<std::uint64_t>(signal.prompt_cache_miss_tokens));
  attrs.emplace("actual_cost_estimate_usd", signal.actual_cost_estimate_usd);
  attrs.emplace("reasoning_mode_requested", signal.reasoning_mode_requested);
  attrs.emplace("reasoning_mode_effective", signal.reasoning_mode_effective);
  attrs.emplace("outcome", signal.outcome);
  attrs.emplace("result_code",
                signal.result_code.empty() ? std::string("none")
                                           : signal.result_code);
  attrs.emplace("result_code_category",
                signal.result_code_category.empty()
                    ? std::string("none")
                    : signal.result_code_category);
  attrs.emplace("error_stage",
                signal.error_stage.empty() ? std::string("none")
                                           : signal.error_stage);
  attrs.emplace("attempted_routes", join_values(signal.attempted_routes, ","));
  attrs.emplace("route_attempt_count",
                static_cast<std::uint64_t>(signal.attempted_routes.size()));
  attrs.emplace("retryable", signal.retryable);
  attrs.emplace("safe_to_replan", signal.safe_to_replan);
  attrs.emplace("detail_ref", signal.detail_ref);

  return infra::tracing::SpanDescriptor{
      .name = std::string(llm_trace_span_name(signal.kind)),
      .kind = span_kind(signal.kind),
      .start_ts_unix_ms = signal.completed_at_ms -
                          static_cast<std::int64_t>(signal.latency_ms),
      .attrs = std::move(attrs),
      .links = {},
  };
}

void LLMTraceBridge::record_success(const std::string& detail_ref) {
  ++emitted_total_;
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void LLMTraceBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::llm::observability