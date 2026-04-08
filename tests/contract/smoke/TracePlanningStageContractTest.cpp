#include <exception>
#include <iostream>
#include <string>

#include "tracing/TraceTypes.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::tracing::TraceContext make_active_planning_context() {
  using dasall::infra::tracing::TraceContext;
  using dasall::infra::tracing::TraceContextState;

  return TraceContext{
      .trace_id = std::string("4bf92f3577b34da6a3ce929d0e0e4736"),
      .span_id = std::string("00f067aa0ba902b7"),
      .trace_flags = 0x01,
      .trace_state = std::string("rojo=00f067aa0ba902b7"),
      .parent_span_id = std::string("b7ad6b7169203331"),
      .state = TraceContextState::Active,
      .is_remote = false,
  };
}

void test_planning_stage_trace_contract_keeps_stage_budget_and_correlation_observable() {
  using dasall::infra::tracing::SpanDescriptor;
  using dasall::infra::tracing::SpanEndResult;
  using dasall::infra::tracing::SpanKind;
  using dasall::infra::tracing::SpanStatusCode;
  using dasall::infra::tracing::TraceAttributeMap;
  using dasall::infra::tracing::TraceAttributeValue;
  using dasall::infra::tracing::planning_stage_outcome_is_consistent;
  using dasall::infra::tracing::planning_stage_trace_has_correlation;
  using dasall::infra::tracing::span_has_planning_stage_contract;
  using dasall::tests::support::assert_true;

  const auto context = make_active_planning_context();
  const SpanDescriptor descriptor{
      .name = std::string("runtime.planning"),
      .kind = SpanKind::Internal,
      .start_ts_unix_ms = 1712563200000,
      .attrs = TraceAttributeMap{
          {std::string(dasall::infra::tracing::kPlanningTraceStageAttrKey),
           TraceAttributeValue{std::string(dasall::infra::tracing::kPlanningTraceStageAttrValue)}},
          {std::string(dasall::infra::tracing::kPlanningTraceBudgetAttrKey),
           TraceAttributeValue{std::int64_t{250}}},
          {std::string(dasall::infra::tracing::kPlanningTraceOutcomeAttrKey),
           TraceAttributeValue{std::string("success")}},
      },
      .links = {},
  };
  const SpanEndResult end_result{
      .end_ts_unix_ms = 1712563200125,
      .status_code = SpanStatusCode::Ok,
      .status_message = std::string(),
      .dropped_attr_count = 0,
  };

  assert_true(span_has_planning_stage_contract(descriptor),
              "planning trace spans should keep stage=planning and positive budget_ms inside the frozen trace attrs boundary");
  assert_true(planning_stage_trace_has_correlation(context),
              "planning trace spans should keep active trace_id/span_id correlation inside the frozen TraceContext boundary");
  assert_true(planning_stage_outcome_is_consistent(descriptor, end_result),
              "planning trace success observations should keep a successful span outcome without leaking extra shared objects");
}

void test_planning_stage_trace_contract_requires_explicit_degraded_signal() {
  using dasall::infra::tracing::SpanDescriptor;
  using dasall::infra::tracing::SpanEndResult;
  using dasall::infra::tracing::SpanKind;
  using dasall::infra::tracing::SpanStatusCode;
  using dasall::infra::tracing::TraceAttributeMap;
  using dasall::infra::tracing::TraceAttributeValue;
  using dasall::infra::tracing::TraceContext;
  using dasall::infra::tracing::TraceContextState;
  using dasall::infra::tracing::planning_stage_outcome_is_consistent;
  using dasall::infra::tracing::planning_stage_trace_has_correlation;
  using dasall::infra::tracing::span_has_planning_stage_contract;
  using dasall::tests::support::assert_true;

  const SpanDescriptor degraded_descriptor{
      .name = std::string("runtime.planning"),
      .kind = SpanKind::Internal,
      .start_ts_unix_ms = 1712563200200,
      .attrs = TraceAttributeMap{
          {std::string(dasall::infra::tracing::kPlanningTraceStageAttrKey),
           TraceAttributeValue{std::string(dasall::infra::tracing::kPlanningTraceStageAttrValue)}},
          {std::string(dasall::infra::tracing::kPlanningTraceBudgetAttrKey),
           TraceAttributeValue{std::uint64_t{300}}},
          {std::string(dasall::infra::tracing::kPlanningTraceOutcomeAttrKey),
           TraceAttributeValue{std::string("degraded")}},
      },
      .links = {},
  };
  const SpanEndResult degraded_end_result{
      .end_ts_unix_ms = 1712563200400,
      .status_code = SpanStatusCode::Error,
      .status_message = std::string("planning budget exhausted"),
      .dropped_attr_count = 1,
  };
  const SpanEndResult inconsistent_end_result{
      .end_ts_unix_ms = 1712563200400,
      .status_code = SpanStatusCode::Ok,
      .status_message = std::string(),
      .dropped_attr_count = 0,
  };
  const TraceContext invalid_context{
      .trace_id = std::string(),
      .span_id = std::string(),
      .trace_flags = 0x00,
      .trace_state = std::string(),
      .parent_span_id = std::string(),
      .state = TraceContextState::Noop,
      .is_remote = false,
  };

  assert_true(span_has_planning_stage_contract(degraded_descriptor),
              "degraded planning spans should still keep the frozen stage/budget attrs intact");
  assert_true(planning_stage_outcome_is_consistent(degraded_descriptor, degraded_end_result),
              "degraded planning observations should surface an explicit error span outcome so degradation remains observable");
  assert_true(!planning_stage_outcome_is_consistent(degraded_descriptor, inconsistent_end_result),
              "degraded planning observations should reject non-error span endings because they hide the degraded path");
  assert_true(!planning_stage_trace_has_correlation(invalid_context),
              "planning trace contract should reject noop contexts because planning spans must keep active trace_id/span_id correlation");
}

}  // namespace

int main() {
  try {
    test_planning_stage_trace_contract_keeps_stage_budget_and_correlation_observable();
    test_planning_stage_trace_contract_requires_explicit_degraded_signal();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}