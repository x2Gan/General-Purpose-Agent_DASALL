#include "tracing/SpanImpl.h"

#include <atomic>

namespace dasall::infra::tracing {
namespace {

std::atomic<std::int64_t> g_span_end_timestamp_seed{1712448000000};

[[nodiscard]] std::int64_t next_end_timestamp() {
  return g_span_end_timestamp_seed.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

SpanImpl::SpanImpl(SpanDescriptor descriptor,
                   TraceContext context,
                   TraceContext parent_context,
                   SamplingDecision sampling_decision)
    : descriptor_(std::move(descriptor)),
      context_(std::move(context)),
      parent_context_(std::move(parent_context)),
      sampling_decision_(std::move(sampling_decision)),
      recording_(sampling_decision_.decision != SamplingDecisionKind::Drop),
      attrs_(recording_ ? descriptor_.attrs : TraceAttributeMap{}) {}

void SpanImpl::set_attribute(std::string_view key, const TraceAttributeValue& value) {
  if (ended_ || !recording_) {
    return;
  }

  if (!is_valid_trace_attr_key(key) || !is_valid_trace_attribute_value(value)) {
    ++dropped_attr_count_;
    return;
  }

  attrs_.insert_or_assign(std::string(key), value);
}

void SpanImpl::add_event(std::string_view name, const TraceAttributeMap& attrs) {
  if (ended_ || !recording_ || name.empty() || !is_printable_ascii(name) ||
      !trace_attributes_are_serializable(attrs)) {
    return;
  }

  ++event_count_;
}

void SpanImpl::set_status(SpanStatusCode code, std::string_view message) {
  if (ended_ || !recording_ || code == SpanStatusCode::Unset ||
      status_code_ == SpanStatusCode::Ok) {
    return;
  }

  if (code == SpanStatusCode::Ok) {
    status_code_ = code;
    status_message_.clear();
    return;
  }

  if (!is_printable_ascii(message)) {
    return;
  }

  status_code_ = SpanStatusCode::Error;
  status_message_ = std::string(message);
}

SpanEndResult SpanImpl::end(std::optional<std::int64_t> end_ts_unix_ms) {
  if (!ended_) {
    ended_ = true;
    end_result_ = SpanEndResult{
        .end_ts_unix_ms = end_ts_unix_ms.has_value() ? end_ts_unix_ms
                                                     : std::optional<std::int64_t>(next_end_timestamp()),
        .status_code = status_code_,
        .status_message = status_message_,
        .dropped_attr_count = dropped_attr_count_,
    };
  }

  return end_result_;
}

TraceContext SpanImpl::get_context() const {
  return context_;
}

const TraceContext& SpanImpl::parent_context() const {
  return parent_context_;
}

bool SpanImpl::has_ended() const {
  return ended_;
}

std::size_t SpanImpl::accepted_attribute_count() const {
  return attrs_.size();
}

std::size_t SpanImpl::accepted_event_count() const {
  return event_count_;
}

SpanStatusCode SpanImpl::status_code() const {
  return status_code_;
}

const std::string& SpanImpl::status_message() const {
  return status_message_;
}

const SamplingDecision& SpanImpl::sampling_decision() const {
  return sampling_decision_;
}

bool SpanImpl::is_recording() const {
  return recording_;
}

bool SpanImpl::is_sampled() const {
  return sampling_decision_.decision == SamplingDecisionKind::RecordAndSample;
}

const TraceAttributeMap& SpanImpl::attributes() const {
  return attrs_;
}

const SpanEndResult& SpanImpl::end_result() const {
  return end_result_;
}

}  // namespace dasall::infra::tracing