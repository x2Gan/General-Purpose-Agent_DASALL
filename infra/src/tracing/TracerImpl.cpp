#include "tracing/TracerImpl.h"

#include <atomic>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "tracing/SpanImpl.h"

namespace dasall::infra::tracing {
namespace {

std::atomic<std::uint64_t> g_trace_id_seed{1};
std::atomic<std::uint64_t> g_span_id_seed{1};
thread_local std::optional<TraceContext> g_active_context;

[[nodiscard]] std::string encode_hex_identifier(
    std::uint64_t value,
    std::size_t width) {
  std::ostringstream stream;
  stream << std::hex << std::nouppercase << std::setfill('0')
         << std::setw(static_cast<int>(width)) << value;

  auto encoded = stream.str();
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

[[nodiscard]] std::string next_trace_id() {
  return encode_hex_identifier(
      g_trace_id_seed.fetch_add(1, std::memory_order_relaxed),
      kTraceIdHexLength);
}

[[nodiscard]] std::string next_span_id() {
  return encode_hex_identifier(
      g_span_id_seed.fetch_add(1, std::memory_order_relaxed),
      kSpanIdHexLength);
}

}  // namespace

TracerImpl::TracerImpl(TracerScope scope)
    : scope_(std::move(scope)) {}

std::shared_ptr<ISpan> TracerImpl::start_span(
    const SpanDescriptor& descriptor,
    const TraceContext* parent) {
  if (!descriptor.is_valid()) {
    return {};
  }

  const auto resolved_parent = resolve_parent_context(parent);
  const auto parent_is_active =
      resolved_parent.state == TraceContextState::Active && resolved_parent.is_valid();

  TraceContext context{
      .trace_id = parent_is_active ? resolved_parent.trace_id : next_trace_id(),
      .span_id = next_span_id(),
      .trace_flags = parent_is_active ? resolved_parent.trace_flags : static_cast<std::uint8_t>(0x01),
      .trace_state = parent_is_active ? resolved_parent.trace_state : std::string(),
      .parent_span_id = parent_is_active ? resolved_parent.span_id : std::string(),
      .state = TraceContextState::Active,
      .is_remote = false,
  };

  last_started_context_ = context;
  return std::make_shared<SpanImpl>(descriptor, context, resolved_parent);
}

void TracerImpl::with_active_span(
    const std::shared_ptr<ISpan>& span,
    const ActiveSpanCallback& fn) {
  const auto previous_context = g_active_context;
  if (span) {
    const auto context = span->get_context();
    if (context.state == TraceContextState::Active && context.is_valid()) {
      g_active_context = context;
    } else {
      g_active_context.reset();
    }
  }

  try {
    if (fn) {
      fn();
    }
  } catch (...) {
    g_active_context = previous_context;
    throw;
  }

  g_active_context = previous_context;
}

TraceContext TracerImpl::current_context() const {
  if (g_active_context.has_value() && g_active_context->state == TraceContextState::Active &&
      g_active_context->is_valid()) {
    return *g_active_context;
  }

  return TraceContext::noop();
}

const TracerScope& TracerImpl::scope() const {
  return scope_;
}

const std::optional<TraceContext>& TracerImpl::last_started_context() const {
  return last_started_context_;
}

TraceContext TracerImpl::resolve_parent_context(const TraceContext* parent) const {
  if (parent != nullptr) {
    if (parent->state == TraceContextState::Active && parent->is_valid()) {
      return *parent;
    }

    return TraceContext::noop();
  }

  return current_context();
}

}  // namespace dasall::infra::tracing