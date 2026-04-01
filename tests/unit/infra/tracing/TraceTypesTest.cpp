#include <exception>
#include <iostream>
#include <limits>
#include <string>

#include "tracing/TraceTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_trace_types_accept_valid_context_descriptor_and_snapshot_defaults() {
  using dasall::infra::tracing::ExportBatchReport;
  using dasall::infra::tracing::SamplingDecision;
  using dasall::infra::tracing::SamplingDecisionKind;
  using dasall::infra::tracing::SpanDescriptor;
  using dasall::infra::tracing::SpanEndResult;
  using dasall::infra::tracing::SpanKind;
  using dasall::infra::tracing::SpanLink;
  using dasall::infra::tracing::SpanStatusCode;
  using dasall::infra::tracing::TraceAttributeMap;
  using dasall::infra::tracing::TraceAttributeValue;
  using dasall::infra::tracing::TraceContext;
  using dasall::infra::tracing::TraceContextState;
  using dasall::infra::tracing::TraceModuleSnapshot;
  using dasall::infra::tracing::TracerScope;
  using dasall::tests::support::assert_true;

  const TraceContext active_context{
      .trace_id = std::string("4bf92f3577b34da6a3ce929d0e0e4736"),
      .span_id = std::string("00f067aa0ba902b7"),
      .trace_flags = 0x01,
      .trace_state = std::string("rojo=00f067aa0ba902b7"),
      .parent_span_id = std::string("b7ad6b7169203331"),
      .state = TraceContextState::Active,
      .is_remote = true,
  };
  assert_true(active_context.is_valid(),
              "TraceContext should accept lowercase hex trace_id/span_id pairs and printable tracestate values when state is active");

  const SpanLink link{
      .context = active_context,
      .attrs = TraceAttributeMap{{"request_id", TraceAttributeValue{std::string("req-trace-001")}}},
  };

  const SpanDescriptor descriptor{
      .name = std::string("runtime.tool_call"),
      .kind = SpanKind::Internal,
      .start_ts_unix_ms = 1711958400000,
      .attrs = TraceAttributeMap{
          {"tool_name", TraceAttributeValue{std::string("search")}},
          {"retry_count", TraceAttributeValue{std::int64_t{1}}},
          {"cache_hit", TraceAttributeValue{true}},
          {"elapsed_ms", TraceAttributeValue{12.5}},
      },
      .links = {link},
  };
  assert_true(descriptor.is_valid(),
              "SpanDescriptor should accept a stable span name, printable attrs, optional start timestamp, and active links");

  const SpanEndResult end_result{
      .end_ts_unix_ms = 1711958400100,
      .status_code = SpanStatusCode::Error,
      .status_message = std::string("timeout"),
      .dropped_attr_count = 1,
  };
  assert_true(end_result.is_valid(),
              "SpanEndResult should accept an explicit end timestamp and error message once the span is ended");

  const SamplingDecision decision{
      .decision = SamplingDecisionKind::RecordAndSample,
      .reason = std::string("parent_based_root"),
      .sampler_desc = std::string("ParentBased{AlwaysOn}"),
  };
  assert_true(decision.is_valid(),
              "SamplingDecision should keep explicit reason and sampler description once the head-sampling result is frozen");

  const ExportBatchReport report{
      .batch_size = 4,
      .success_count = 4,
      .failure_count = 0,
      .latency_ms = 12,
  };
  assert_true(report.is_valid(),
              "ExportBatchReport should accept success/failure totals that do not exceed the batch size");

  const TraceModuleSnapshot snapshot{
      .queue_depth = 16,
      .dropped_total = 1,
      .exporter_state = std::string("file"),
      .degraded = false,
  };
  assert_true(snapshot.is_valid(),
              "TraceModuleSnapshot should accept explicit exporter state and queue counters for tracing health reporting");

  const TracerScope scope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
  assert_true(scope.is_valid(),
              "TracerScope should remain valid when a non-empty instrumentation scope name is supplied");
}

void test_trace_types_reject_invalid_context_descriptor_and_end_states() {
  using dasall::infra::tracing::SpanDescriptor;
  using dasall::infra::tracing::SpanEndResult;
  using dasall::infra::tracing::SpanKind;
  using dasall::infra::tracing::SpanStatusCode;
  using dasall::infra::tracing::TraceAttributeMap;
  using dasall::infra::tracing::TraceAttributeValue;
  using dasall::infra::tracing::TraceContext;
  using dasall::infra::tracing::TraceContextState;
  using dasall::tests::support::assert_true;

  const TraceContext invalid_active_context{
      .trace_id = std::string("invalid"),
      .span_id = std::string(),
      .trace_flags = 0x01,
      .trace_state = std::string("rojo=bad"),
      .parent_span_id = std::string(),
      .state = TraceContextState::Active,
      .is_remote = false,
  };
  assert_true(!invalid_active_context.is_valid(),
              "TraceContext should reject active contexts whose trace_id/span_id are not lowercase hex with the frozen lengths");

  const TraceContext invalid_noop_context{
      .trace_id = std::string("4bf92f3577b34da6a3ce929d0e0e4736"),
      .span_id = std::string(),
      .trace_flags = 0x00,
      .trace_state = std::string(),
      .parent_span_id = std::string(),
      .state = TraceContextState::Noop,
      .is_remote = false,
  };
  assert_true(!invalid_noop_context.is_valid(),
              "TraceContext should reject noop state when ids are not kept explicitly empty");

  const SpanDescriptor invalid_descriptor{
      .name = std::string(),
      .kind = SpanKind::Internal,
      .start_ts_unix_ms = -1,
      .attrs = TraceAttributeMap{{"", TraceAttributeValue{std::string("bad")}}},
      .links = {},
  };
  assert_true(!invalid_descriptor.is_valid(),
              "SpanDescriptor should reject empty span names, negative explicit timestamps, and empty attribute keys");

  const SpanDescriptor invalid_descriptor_with_non_finite_attr{
      .name = std::string("runtime.non_finite"),
      .kind = SpanKind::Internal,
      .start_ts_unix_ms = 1711958400000,
      .attrs = TraceAttributeMap{{
          "elapsed_ms",
          TraceAttributeValue{std::numeric_limits<double>::infinity()},
      }},
      .links = {},
  };
  assert_true(!invalid_descriptor_with_non_finite_attr.is_valid(),
              "SpanDescriptor should reject non-finite floating-point attribute values because they are not serialization-stable");

  const SpanEndResult invalid_end_result{
      .end_ts_unix_ms = 1711958400100,
      .status_code = SpanStatusCode::Ok,
      .status_message = std::string("should-not-be-present"),
      .dropped_attr_count = 0,
  };
  assert_true(!invalid_end_result.is_valid(),
              "SpanEndResult should reject status messages on Ok/Unset states to keep OTel status semantics binary");
}

void test_trace_types_keep_operation_status_and_batch_report_binary() {
  using dasall::contracts::ResultCode;
  using dasall::infra::tracing::ExportBatchReport;
  using dasall::infra::tracing::SamplingDecision;
  using dasall::infra::tracing::SamplingDecisionKind;
  using dasall::infra::tracing::TraceOperationStatus;
  using dasall::tests::support::assert_true;

  const auto success = TraceOperationStatus::success("trace-provider://ready");
  assert_true(success.ok && !success.error.has_value(),
              "TraceOperationStatus::success should produce a binary success without contracts error payloads");

  const auto failure = TraceOperationStatus::failure(
      ResultCode::ValidationFieldMissing,
      std::string("trace config must stay explicit"),
      std::string("tracing.init"),
      std::string("TraceOperationStatusTest"));
  assert_true(!failure.ok,
              "TraceOperationStatus::failure should keep an explicit failure state for provider/tracer lifecycle APIs");
  assert_true(failure.references_only_contract_error_types(),
              "TraceOperationStatus failures should stay inside contracts ResultCode/ErrorInfo types");

  const ExportBatchReport invalid_report{
      .batch_size = 2,
      .success_count = 2,
      .failure_count = 1,
      .latency_ms = 8,
  };
  assert_true(!invalid_report.is_valid(),
              "ExportBatchReport should reject success/failure counters whose sum exceeds the exported batch size");

  const SamplingDecision invalid_decision{
      .decision = SamplingDecisionKind::Drop,
      .reason = std::string(),
      .sampler_desc = std::string(),
  };
  assert_true(!invalid_decision.is_valid(),
              "SamplingDecision should reject empty reason and sampler description placeholders once the decision is materialized");
}

}  // namespace

int main() {
  try {
    test_trace_types_accept_valid_context_descriptor_and_snapshot_defaults();
    test_trace_types_reject_invalid_context_descriptor_and_end_states();
    test_trace_types_keep_operation_status_and_batch_report_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
