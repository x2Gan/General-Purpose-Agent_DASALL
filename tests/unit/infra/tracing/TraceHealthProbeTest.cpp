#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "tracing/TraceHealthProbe.h"
#include "tracing/TracerProviderImpl.h"
#include "tracing/ITracer.h"
#include "tracing/ISpan.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::tracing::TraceOperationStatus make_failure_status(
    dasall::infra::tracing::TraceErrorCode code,
    std::string stage) {
  const auto mapping = dasall::infra::tracing::map_trace_error_code(code);
  return dasall::infra::tracing::TraceOperationStatus::failure(
      mapping.result_code,
      std::string("tracing health test failure"),
      std::move(stage),
      std::string("TraceHealthProbeTest:") +
          std::string(dasall::infra::tracing::trace_error_code_name(code)));
}

[[nodiscard]] dasall::infra::tracing::TraceModuleSnapshot make_snapshot(
    std::string exporter_state,
    bool exporter_degraded) {
  return dasall::infra::tracing::TraceModuleSnapshot{
      .queue_depth = 0U,
      .dropped_total = 0U,
      .exporter_state = std::move(exporter_state),
      .degraded = exporter_degraded,
  };
}

[[nodiscard]] dasall::infra::tracing::TracerScope make_scope() {
  return dasall::infra::tracing::TracerScope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::tracing::SpanDescriptor make_descriptor(std::string name) {
  return dasall::infra::tracing::SpanDescriptor{
      .name = std::move(name),
      .kind = dasall::infra::tracing::SpanKind::Internal,
      .start_ts_unix_ms = 1712534400000,
      .attrs = {{"component", dasall::infra::tracing::TraceAttributeValue{std::string("tracing")}}},
      .links = {},
  };
}

[[nodiscard]] dasall::infra::tracing::TraceConfig make_unsupported_exporter_config() {
  dasall::infra::tracing::TraceConfig config;
  config.batch.enabled = false;
  config.exporter.type = std::string("otlp");
  config.exporter.otlp_endpoint = std::string("http://127.0.0.1:4318");
  return config;
}

void start_and_end_span(dasall::infra::tracing::TracerProviderImpl& provider,
                        std::string span_name,
                        std::int64_t end_ts_unix_ms) {
  const auto tracer = provider.get_tracer(make_scope());
  const auto span = tracer->start_span(make_descriptor(std::move(span_name)), nullptr);
  (void)span->end(end_ts_unix_ms);
}

void test_trace_health_probe_enters_degraded_after_consecutive_failures() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TraceHealthProbe;
  using dasall::tests::support::assert_true;

  TraceHealthProbe probe(2U);

  const auto first_observation = probe.observe_result(
      make_failure_status(TraceErrorCode::ExportTimeout,
                          std::string("tracing.exporter.export_batch")),
      make_snapshot("noop", true));

  assert_true(first_observation.ok && !probe.is_degraded() &&
                  probe.consecutive_failure_total() == 1U && probe.snapshot().is_valid() &&
                  probe.snapshot().last_error_code == TraceErrorCode::ExportTimeout,
              "TraceHealthProbe should keep monitoring after the first exporter failure without entering degraded mode before the configured threshold is reached");

  const auto second_observation = probe.observe_result(
      make_failure_status(TraceErrorCode::ExportTimeout,
                          std::string("tracing.exporter.export_batch")),
      make_snapshot("noop", true));

  assert_true(second_observation.ok && probe.is_degraded() &&
                  probe.degrade_enter_total() == 1U && probe.snapshot().degraded_mode &&
                  probe.snapshot().detail_ref.find("TRC_E_EXPORT_TIMEOUT") != std::string::npos,
              "TraceHealthProbe should enter degraded mode observably once continuous exporter failures cross the configured threshold");
}

void test_trace_health_probe_recovers_to_healthy_after_success() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TraceHealthProbe;
  using dasall::tests::support::assert_true;

  TraceHealthProbe probe(2U);
  (void)probe.observe_result(
      make_failure_status(TraceErrorCode::ExportFailure,
                          std::string("tracing.exporter.export_batch")),
      make_snapshot("noop", true));
  (void)probe.observe_result(
      make_failure_status(TraceErrorCode::ExportFailure,
                          std::string("tracing.exporter.export_batch")),
      make_snapshot("noop", true));

  const auto recovery_result = probe.observe_result(
      dasall::infra::tracing::TraceOperationStatus::success("trace-exporter://noop"),
      make_snapshot("noop", false));

  assert_true(recovery_result.ok && !probe.is_degraded() &&
                  probe.recovery_success_total() == 1U &&
                  probe.consecutive_failure_total() == 0U &&
                  !probe.snapshot().last_error_code.has_value() &&
                  probe.snapshot().detail_ref.find("healthy") != std::string::npos,
              "TraceHealthProbe should clear degraded mode and reset failure counters after a healthy exporter result is observed");
}

void test_trace_health_probe_rejects_invalid_snapshot_inputs() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TraceHealthProbe;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TraceHealthProbe probe;
  const auto result = probe.enter_degraded(
      TraceErrorCode::ExportFailure,
      std::string("missing snapshot"),
      dasall::infra::tracing::TraceModuleSnapshot{
          .queue_depth = 0U,
          .dropped_total = 0U,
          .exporter_state = std::string(),
          .degraded = false,
      });

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code ==
                      map_trace_error_code(TraceErrorCode::ConfigInvalid).result_code,
              "TraceHealthProbe should reject degraded-mode transitions when the supplied tracing snapshot is invalid");
}

void test_tracer_provider_exposes_private_trace_health_snapshot() {
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_unsupported_exporter_config()).ok,
              "TracerProviderImpl should initialize before tracing health snapshot integration is exercised");

  start_and_end_span(provider, std::string("trace.health.integration"), 2400);
  const auto snapshot = provider.health_snapshot();

  assert_true(snapshot.is_valid() && !snapshot.degraded_mode &&
                  snapshot.consecutive_failure_total == 1U &&
                  snapshot.module_snapshot.degraded &&
            snapshot.module_snapshot.exporter_state == "otlp" &&
                  snapshot.last_error_code.has_value(),
              "TracerProviderImpl should expose the tracing private health snapshot after the pipeline records a degraded exporter observation");
}

}  // namespace

int main() {
  try {
    test_trace_health_probe_enters_degraded_after_consecutive_failures();
    test_trace_health_probe_recovers_to_healthy_after_success();
    test_trace_health_probe_rejects_invalid_snapshot_inputs();
    test_tracer_provider_exposes_private_trace_health_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}