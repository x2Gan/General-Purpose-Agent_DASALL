#include <exception>
#include <iostream>
#include <string>

#include "tracing/ContextPropagationAdapter.h"
#include "tracing/TraceErrors.h"
#include "support/TestAssertions.h"

namespace {

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

void test_context_propagation_adapter_round_trips_valid_context() {
  using dasall::infra::tracing::ContextPropagationAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ContextPropagationAdapter propagator;
  dasall::infra::tracing::TraceCarrier carrier;

  propagator.inject(make_context(), carrier);
  assert_true(carrier.contains("traceparent") && carrier.contains("tracestate") &&
                  propagator.last_operation_status().ok,
              "ContextPropagationAdapter should inject lowercase traceparent/tracestate entries for a valid active context");

  const auto extracted = propagator.extract(carrier);
  assert_true(extracted.is_valid() && extracted.is_remote &&
                  propagator.last_operation_status().ok,
              "ContextPropagationAdapter should extract a valid remote TraceContext from a valid carrier");
  assert_equal(std::string("4bf92f3577b34da6a3ce929d0e0e4736"), extracted.trace_id,
               "ContextPropagationAdapter should preserve trace_id across inject/extract round-trips");
  assert_equal(std::string("00f067aa0ba902b7"), extracted.span_id,
               "ContextPropagationAdapter should preserve span_id across inject/extract round-trips");
}

void test_context_propagation_adapter_handles_noop_and_invalid_contexts() {
  using dasall::infra::tracing::ContextPropagationAdapter;
  using dasall::infra::tracing::TraceContext;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  ContextPropagationAdapter propagator;
  dasall::infra::tracing::TraceCarrier carrier{
      {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
      {"tracestate", "rojo=00f067aa0ba902b7"},
  };

  propagator.inject(TraceContext::noop(), carrier);
  assert_true(carrier.empty() && propagator.last_operation_status().ok,
              "ContextPropagationAdapter should clear propagation headers and treat explicit noop injection as success");

  const auto noop_context = propagator.extract(carrier);
  assert_true(noop_context.state == dasall::infra::tracing::TraceContextState::Noop &&
                  noop_context.is_valid() && propagator.last_operation_status().ok,
              "ContextPropagationAdapter should return explicit noop context when no carrier trace headers are present");

  carrier["TraceParent"] = "00-invalid-traceparent";
  const auto invalid_context = propagator.extract(carrier);
  assert_true(invalid_context.state == dasall::infra::tracing::TraceContextState::Invalid &&
                  invalid_context.is_valid() && !propagator.last_operation_status().ok &&
                  propagator.last_operation_status().references_only_contract_error_types() &&
                  propagator.last_operation_status().result_code ==
                      map_trace_error_code(TraceErrorCode::InvalidContext).result_code &&
                  propagator.invalid_context_total() == 1U,
              "ContextPropagationAdapter should surface malformed traceparent values as explicit invalid-context failures");
}

}  // namespace

int main() {
  try {
    test_context_propagation_adapter_round_trips_valid_context();
    test_context_propagation_adapter_handles_noop_and_invalid_contexts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}