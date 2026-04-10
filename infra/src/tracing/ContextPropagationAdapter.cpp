#include "tracing/ContextPropagationAdapter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "tracing/TraceErrors.h"

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kTraceParentHeader = "traceparent";
constexpr std::string_view kTraceStateHeader = "tracestate";
constexpr std::string_view kTracePropagationSourceRef = "ContextPropagationAdapter";

[[nodiscard]] TraceOperationStatus make_trace_failure(
    TraceErrorCode code,
    std::string message,
    std::string stage) {
  const auto mapping = map_trace_error_code(code);
  return TraceOperationStatus::failure(mapping.result_code,
                                       std::move(message),
                                       std::move(stage),
                                       std::string(kTracePropagationSourceRef) + ":" +
                                           std::string(trace_error_code_name(code)));
}

[[nodiscard]] bool equals_ignore_case_ascii(
    std::string_view lhs,
    std::string_view rhs) {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](const char left, const char right) {
           return std::tolower(static_cast<unsigned char>(left)) ==
                  std::tolower(static_cast<unsigned char>(right));
         });
}

[[nodiscard]] const std::string* find_header_value(
    const TraceCarrier& carrier,
    const std::string_view& header_name) {
  const auto entry = std::find_if(carrier.begin(),
                                  carrier.end(),
                                  [&header_name](const auto& item) {
                                    return equals_ignore_case_ascii(item.first, header_name);
                                  });
  if (entry != carrier.end()) {
    return &entry->second;
  }

  return nullptr;
}

void erase_header(TraceCarrier& carrier, const std::string_view& header_name) {
  for (auto it = carrier.begin(); it != carrier.end();) {
    if (equals_ignore_case_ascii(it->first, header_name)) {
      it = carrier.erase(it);
    } else {
      ++it;
    }
  }
}

[[nodiscard]] std::string hex_byte(std::uint8_t value) {
  constexpr char kHex[] = "0123456789abcdef";
  return std::string{kHex[(value >> 4) & 0x0f], kHex[value & 0x0f]};
}

[[nodiscard]] std::optional<std::uint8_t> parse_hex_byte(std::string_view value) {
  if (!is_lower_hex_string(value, 2)) {
    return std::nullopt;
  }

  return static_cast<std::uint8_t>(std::stoul(std::string(value), nullptr, 16));
}

}  // namespace

void ContextPropagationAdapter::inject(
    const TraceContext& context,
    TraceCarrier& carrier) const {
  if (context.state == TraceContextState::Noop && context.is_valid()) {
    erase_header(carrier, kTraceParentHeader);
    erase_header(carrier, kTraceStateHeader);
    set_last_operation_status(
        TraceOperationStatus::success("trace-propagation://noop-injected"));
    return;
  }

  if (context.state != TraceContextState::Active || !context.is_valid()) {
    erase_header(carrier, kTraceParentHeader);
    erase_header(carrier, kTraceStateHeader);
    ++invalid_context_total_;
    set_last_operation_status(make_trace_failure(
        TraceErrorCode::InvalidContext,
        "inject() requires a valid active TraceContext or explicit noop context",
        "tracing.propagation.inject"));
    return;
  }

  carrier[std::string(kTraceParentHeader)] =
      std::string("00-") + context.trace_id + "-" + context.span_id + "-" +
      hex_byte(context.trace_flags);
  if (context.trace_state.empty()) {
    erase_header(carrier, kTraceStateHeader);
  } else {
    carrier[std::string(kTraceStateHeader)] = context.trace_state;
  }

  set_last_operation_status(
      TraceOperationStatus::success("trace-propagation://injected"));
}

TraceContext ContextPropagationAdapter::extract(const TraceCarrier& carrier) const {
  const auto* traceparent = find_header_value(carrier, kTraceParentHeader);
  if (traceparent == nullptr) {
    set_last_operation_status(
        TraceOperationStatus::success("trace-propagation://noop-extracted"));
    return TraceContext::noop();
  }

  if (traceparent->size() != 55 || (*traceparent)[2] != '-' || (*traceparent)[35] != '-' ||
      (*traceparent)[52] != '-') {
    ++invalid_context_total_;
    set_last_operation_status(make_trace_failure(
        TraceErrorCode::InvalidContext,
        "traceparent must follow the W3C 00-trace-id-parent-id-trace-flags format",
        "tracing.propagation.extract"));
    return TraceContext::invalid();
  }

  const std::string_view version = std::string_view(*traceparent).substr(0, 2);
  const std::string_view trace_id = std::string_view(*traceparent).substr(3, 32);
  const std::string_view span_id = std::string_view(*traceparent).substr(36, 16);
  const std::string_view trace_flags = std::string_view(*traceparent).substr(53, 2);

  const auto parsed_trace_flags = parse_hex_byte(trace_flags);
  if (version != "00" || !is_lower_hex_string(trace_id, kTraceIdHexLength) ||
      !is_lower_hex_string(span_id, kSpanIdHexLength) || !parsed_trace_flags.has_value()) {
    ++invalid_context_total_;
    set_last_operation_status(make_trace_failure(
        TraceErrorCode::InvalidContext,
        "traceparent contains invalid version, trace-id, parent-id, or trace-flags",
        "tracing.propagation.extract"));
    return TraceContext::invalid();
  }

  std::string trace_state;
  const auto* tracestate = find_header_value(carrier, kTraceStateHeader);
  if (tracestate != nullptr && is_printable_ascii(*tracestate) &&
      tracestate->size() <= kTraceStateMaxLength) {
    trace_state = *tracestate;
  }

  set_last_operation_status(
      TraceOperationStatus::success("trace-propagation://extracted"));
  return TraceContext{
      .trace_id = std::string(trace_id),
      .span_id = std::string(span_id),
      .trace_flags = *parsed_trace_flags,
      .trace_state = trace_state,
      .parent_span_id = std::string(),
      .state = TraceContextState::Active,
      .is_remote = true,
  };
}

const TraceOperationStatus& ContextPropagationAdapter::last_operation_status() const {
  return last_operation_status_;
}

std::uint64_t ContextPropagationAdapter::invalid_context_total() const {
  return invalid_context_total_;
}

void ContextPropagationAdapter::set_last_operation_status(TraceOperationStatus status) const {
  last_operation_status_ = std::move(status);
}

}  // namespace dasall::infra::tracing