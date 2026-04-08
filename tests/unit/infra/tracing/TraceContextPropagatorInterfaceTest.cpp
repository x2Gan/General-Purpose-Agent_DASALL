#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "tracing/ITraceContextPropagator.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::string hex_byte(std::uint8_t value) {
  constexpr char kHex[] = "0123456789abcdef";
  return std::string{ kHex[(value >> 4) & 0x0f], kHex[value & 0x0f] };
}

class MapTraceContextPropagator final : public dasall::infra::tracing::ITraceContextPropagator {
 public:
  void inject(
      const dasall::infra::tracing::TraceContext& context,
      dasall::infra::tracing::TraceCarrier& carrier) const override {
    if (context.state != dasall::infra::tracing::TraceContextState::Active || !context.is_valid()) {
      return;
    }

    carrier["traceparent"] =
        std::string("00-") + context.trace_id + "-" + context.span_id + "-" +
        hex_byte(context.trace_flags);
    if (!context.trace_state.empty()) {
      carrier["tracestate"] = context.trace_state;
    }
  }

  [[nodiscard]] dasall::infra::tracing::TraceContext extract(
      const dasall::infra::tracing::TraceCarrier& carrier) const override {
    const auto traceparent_it = carrier.find("traceparent");
    if (traceparent_it == carrier.end()) {
      return dasall::infra::tracing::TraceContext::noop();
    }

    const std::string& traceparent = traceparent_it->second;
    if (traceparent.size() != 55 || traceparent[2] != '-' || traceparent[35] != '-' ||
        traceparent[52] != '-') {
      return dasall::infra::tracing::TraceContext::invalid();
    }

    const std::string trace_id = traceparent.substr(3, 32);
    const std::string span_id = traceparent.substr(36, 16);
    const std::string flags = traceparent.substr(53, 2);
    if (!dasall::infra::tracing::is_lower_hex_string(trace_id, 32) ||
        !dasall::infra::tracing::is_lower_hex_string(span_id, 16) ||
        !dasall::infra::tracing::is_lower_hex_string(flags, 2)) {
      return dasall::infra::tracing::TraceContext::invalid();
    }

    const auto tracestate_it = carrier.find("tracestate");
    return dasall::infra::tracing::TraceContext{
        .trace_id = trace_id,
        .span_id = span_id,
        .trace_flags = static_cast<std::uint8_t>(std::stoul(flags, nullptr, 16)),
        .trace_state = tracestate_it == carrier.end() ? std::string() : tracestate_it->second,
        .parent_span_id = std::string(),
        .state = dasall::infra::tracing::TraceContextState::Active,
        .is_remote = true,
    };
  }
};

[[nodiscard]] dasall::infra::tracing::TraceContext make_context() {
  return dasall::infra::tracing::TraceContext{
      .trace_id = std::string("4bf92f3577b34da6a3ce929d0e0e4736"),
      .span_id = std::string("00f067aa0ba902b7"),
      .trace_flags = 0x01,
      .trace_state = std::string("rojo=00f067aa0ba902b7"),
      .parent_span_id = std::string("b7ad6b7169203331"),
      .state = dasall::infra::tracing::TraceContextState::Active,
      .is_remote = false,
  };
}

void test_trace_context_propagator_keeps_frozen_method_surface() {
  using dasall::infra::tracing::ITraceContextPropagator;

  static_assert(std::is_same_v<decltype(&ITraceContextPropagator::inject),
                               void (ITraceContextPropagator::*)(
                                   const dasall::infra::tracing::TraceContext&,
                                   dasall::infra::tracing::TraceCarrier&) const>);
  static_assert(std::is_same_v<decltype(&ITraceContextPropagator::extract),
                               dasall::infra::tracing::TraceContext (ITraceContextPropagator::*)(
                                   const dasall::infra::tracing::TraceCarrier&) const>);
}

void test_trace_context_propagator_round_trips_valid_context() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MapTraceContextPropagator propagator;
  dasall::infra::tracing::TraceCarrier carrier;
  propagator.inject(make_context(), carrier);

  assert_true(carrier.contains("traceparent"),
              "ITraceContextPropagator should inject a traceparent carrier entry for valid active contexts");
  assert_true(carrier.contains("tracestate"),
              "ITraceContextPropagator should inject tracestate when the source context carries vendor state");

  const auto extracted = propagator.extract(carrier);
  assert_true(extracted.is_valid() && extracted.is_remote,
              "ITraceContextPropagator should extract a valid remote TraceContext from a valid carrier");
  assert_equal(std::string("4bf92f3577b34da6a3ce929d0e0e4736"), extracted.trace_id,
               "TraceContext propagation should preserve trace_id across inject/extract round-trips");
  assert_equal(std::string("00f067aa0ba902b7"), extracted.span_id,
               "TraceContext propagation should preserve span_id across inject/extract round-trips");
}

void test_trace_context_propagator_handles_noop_and_invalid_inputs() {
  using dasall::tests::support::assert_true;

  MapTraceContextPropagator propagator;
  dasall::infra::tracing::TraceCarrier carrier;
  propagator.inject(dasall::infra::tracing::TraceContext::noop(), carrier);
  assert_true(carrier.empty(),
              "ITraceContextPropagator should keep carrier empty when the source context is explicit noop");

  const auto noop_context = propagator.extract(carrier);
  assert_true(noop_context.state == dasall::infra::tracing::TraceContextState::Noop &&
                  noop_context.is_valid(),
              "ITraceContextPropagator should return explicit noop context when no carrier trace headers are present");

  carrier["traceparent"] = "00-invalid-traceparent";
  const auto invalid_context = propagator.extract(carrier);
  assert_true(invalid_context.state == dasall::infra::tracing::TraceContextState::Invalid &&
                  invalid_context.is_valid(),
              "ITraceContextPropagator should return explicit invalid context when the carrier traceparent is malformed");
}

}  // namespace

int main() {
  try {
    test_trace_context_propagator_keeps_frozen_method_surface();
    test_trace_context_propagator_round_trips_valid_context();
    test_trace_context_propagator_handles_noop_and_invalid_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
