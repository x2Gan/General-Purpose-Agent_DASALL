#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "audit/IAuditLogger.h"
#include "metrics/IMeter.h"
#include "metrics/MetricTypes.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/SpanImpl.h"
#include "tracing/SpanProcessorPipeline.h"
#include "tracing/TraceErrors.h"
#include "tracing/TracerImpl.h"
#include "tracing/TracerProviderImpl.h"
#include "support/TestAssertions.h"

namespace {

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    samples.push_back(sample);
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-runtime-bridge");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> samples;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-runtime-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-runtime-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://trace-runtime-provider-shutdown");
  }

  std::shared_ptr<RecordingMeter> meter_;
  dasall::infra::metrics::MeterScope last_scope{};
};

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool has_metric_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    std::string_view name,
    std::string_view stage,
    std::string_view outcome,
    std::optional<std::string_view> error_code = std::nullopt) {
  return std::any_of(samples.begin(), samples.end(), [&](const auto& sample) {
    const bool matches_identity = sample.identity_ref.name == name;
    const bool matches_labels = sample.labels.stage == stage &&
                                sample.labels.outcome == outcome;
    const bool matches_error = !error_code.has_value() ||
                               sample.labels.error_code == *error_code;
    return matches_identity && matches_labels && matches_error;
  });
}

[[nodiscard]] bool has_audit_action(
    const std::vector<dasall::infra::AuditEvent>& events,
    std::string_view action) {
  return std::any_of(events.begin(), events.end(), [&](const auto& event) {
    return event.action == action;
  });
}

[[nodiscard]] std::optional<dasall::infra::AuditContext> find_audit_context(
    const std::vector<dasall::infra::AuditEvent>& events,
    const std::vector<dasall::infra::AuditContext>& contexts,
    std::string_view action) {
  for (std::size_t index = 0; index < events.size() && index < contexts.size(); ++index) {
    if (events[index].action == action) {
      return contexts[index];
    }
  }

  return std::nullopt;
}

[[nodiscard]] dasall::infra::InfraContext make_infra_context() {
  return dasall::infra::InfraContext{
      .request_id = std::string("req-batch-export-bridge"),
      .session_id = std::string("sess-batch-export-bridge"),
      .trace_id = std::string("trace-batch-export-bridge"),
      .task_id = std::string("task-batch-export-bridge"),
      .parent_task_id = std::string("parent-batch-export-bridge"),
      .lease_id = std::string("lease-batch-export-bridge"),
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

[[nodiscard]] dasall::infra::tracing::TraceConfig make_config(std::string exporter_type,
                                                              bool batch_enabled,
                                                              std::uint32_t max_export_batch_size,
                                                              std::uint32_t export_timeout_ms,
                                                              std::string otlp_endpoint = {},
                                                              std::uint32_t max_queue_size = 8U,
                                                              std::uint32_t schedule_delay_ms = 500U,
                                                              std::string overflow_policy =
                                                                  std::string(dasall::infra::tracing::kTraceOverflowPolicyDropOldest)) {
  dasall::infra::tracing::TraceConfig config;
  config.exporter.type = std::move(exporter_type);
  config.exporter.otlp_endpoint = std::move(otlp_endpoint);
  config.batch.enabled = batch_enabled;
  config.batch.max_queue_size = max_queue_size;
  config.batch.max_export_batch_size = max_export_batch_size;
  config.batch.schedule_delay_ms = schedule_delay_ms;
  config.export_timeout_ms = export_timeout_ms;
  config.overflow_policy = std::move(overflow_policy);
  return config;
}

void start_and_end_span(dasall::infra::tracing::TracerProviderImpl& provider,
                        std::string span_name,
                        std::int64_t end_ts_unix_ms) {
  const auto tracer = provider.get_tracer(make_scope());
  const auto span = tracer->start_span(make_descriptor(std::move(span_name)), nullptr);
  (void)span->end(end_ts_unix_ms);
}

void test_tracer_provider_force_flush_exports_buffered_spans() {
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config("noop", true, 8U, 100U)).ok,
              "TracerProviderImpl should initialize before the buffered export path is exercised");

  start_and_end_span(provider, "trace.force_flush", 2000);
  const auto buffered_snapshot = provider.module_snapshot();
  assert_true(buffered_snapshot.queue_depth == 1U,
              "SpanProcessorPipeline should leave the ended span buffered until force_flush() when the batch threshold is not met");

  const auto flush_status = provider.force_flush(250U);
  const auto report = provider.last_export_report();
  const auto snapshot = provider.module_snapshot();
  assert_true(flush_status.ok && report.batch_size == 1U && report.success_count == 1U &&
                  report.failure_count == 0U && snapshot.queue_depth == 0U &&
                  snapshot.exporter_state == "noop" && !snapshot.degraded,
              "TracerProviderImpl force_flush() should drain the pending batch through the noop exporter");
}

void test_tracer_provider_immediately_exports_file_batches_when_batch_is_disabled() {
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config("file", false, 4U, 10U)).ok,
              "TracerProviderImpl should initialize before the file exporter path is exercised");

  start_and_end_span(provider, "trace.file_export", 2100);

  const auto status = provider.last_pipeline_status();
  const auto report = provider.last_export_report();
  const auto snapshot = provider.module_snapshot();
  const auto rendered_output = provider.last_rendered_output();
  assert_true(status.ok && report.batch_size == 1U && report.success_count == 1U &&
                  report.failure_count == 0U && snapshot.queue_depth == 0U &&
                  snapshot.exporter_state == "file" && !snapshot.degraded &&
                  rendered_output.find("name=trace.file_export") != std::string::npos &&
                  rendered_output.find("trace_id=") != std::string::npos,
              "SpanProcessorPipeline should export ended spans through the file exporter immediately when batch mode is disabled");
}

void test_tracer_provider_surfaces_export_timeout_observably() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config("file", true, 2U, 1U)).ok,
              "TracerProviderImpl should initialize before the export-timeout path is exercised");

  start_and_end_span(provider, "trace.timeout.first", 2200);
  start_and_end_span(provider, "trace.timeout.second", 2201);

  const auto status = provider.last_pipeline_status();
  const auto report = provider.last_export_report();
  const auto snapshot = provider.module_snapshot();
  assert_true(!status.ok &&
                  status.result_code ==
                      map_trace_error_code(TraceErrorCode::ExportTimeout).result_code &&
                  report.batch_size == 2U && report.success_count == 0U &&
                  report.failure_count == 2U && provider.export_failure_total() == 2U &&
                  snapshot.queue_depth == 0U && snapshot.exporter_state == "noop" &&
                  snapshot.degraded,
              "SpanProcessorPipeline should surface export timeout failures and degrade the exporter to noop observably");
}

void test_tracer_provider_surfaces_unsupported_exporter_failure() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_true;

  TracerProviderImpl provider;
  assert_true(provider.init(make_config("otlp", false, 4U, 10U, "http://127.0.0.1:4318")).ok,
              "TracerProviderImpl should initialize before the unsupported exporter path is exercised");

  start_and_end_span(provider, "trace.otlp.failure", 2300);

  const auto status = provider.last_pipeline_status();
  const auto report = provider.last_export_report();
  const auto snapshot = provider.module_snapshot();
  assert_true(!status.ok &&
                  status.result_code ==
                      map_trace_error_code(TraceErrorCode::ExportFailure).result_code &&
                  report.batch_size == 1U && report.success_count == 0U &&
                  report.failure_count == 1U && provider.export_failure_total() == 1U &&
                  snapshot.queue_depth == 0U && snapshot.exporter_state == "noop" &&
                  snapshot.degraded,
              "SpanProcessorPipeline should surface unsupported exporter failures and fall back to noop during the first exporter round");
}

void test_tracer_provider_wires_export_metrics_into_trace_metrics_bridge() {
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);

  TracerProviderImpl provider;
  provider.set_metrics_provider(metrics_provider, "desktop_full");
  assert_true(provider.init(make_config("otlp",
                                        false,
                                        4U,
                                        10U,
                                        "http://127.0.0.1:4318"))
                  .ok,
              "TracerProviderImpl should initialize before runtime bridge metrics wiring is exercised");

  start_and_end_span(provider, "trace.bridge.export_failure", 2400);

  assert_equal(std::string("infra.tracing"),
               metrics_provider->last_scope.name,
               "runtime bridge wiring should request the frozen infra.tracing meter scope");
  assert_true(has_metric_sample(meter->samples,
                                "trace_span_ended_total",
                                "span",
                                "success") &&
                  has_metric_sample(meter->samples,
                                    "trace_export_failure_total",
                                    "export",
                                    "degraded",
                                    std::string_view("TRC_E_EXPORT_FAILURE")) &&
                  has_metric_sample(meter->samples,
                                    "trace_export_latency_ms",
                                    "export",
                                    "degraded",
                                    std::string_view("TRC_E_EXPORT_FAILURE")) &&
                  has_metric_sample(meter->samples,
                                    "trace_batch_queue_depth",
                                    "queue",
                                    "degraded"),
              "SpanProcessorPipeline should push ended-span, export-failure, latency and queue-depth state changes into TraceMetricsBridge when a metrics provider is configured");
}

void test_span_processor_pipeline_wires_health_transition_audit_into_trace_audit_bridge() {
  using dasall::infra::tracing::SpanImpl;
  using dasall::infra::tracing::SpanProcessorPipeline;
  using dasall::infra::tracing::TracerImpl;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto logger = std::make_shared<RecordingAuditLogger>();

  const auto config = make_config("file", true, 2U, 1U, {}, 8U, 5000U);
  SpanProcessorPipeline pipeline(config);
  pipeline.set_metrics_provider(metrics_provider, "edge_balanced");
  pipeline.set_audit_logger(logger, {.infra_context = make_infra_context(),
                   .worker_type = std::string("infra.tracing")});

  TracerImpl tracer(make_scope(), config, nullptr);
  const auto first_span =
    std::dynamic_pointer_cast<SpanImpl>(tracer.start_span(make_descriptor("trace.pipeline.audit.first"), nullptr));
  const auto second_span =
    std::dynamic_pointer_cast<SpanImpl>(tracer.start_span(make_descriptor("trace.pipeline.audit.second"), nullptr));

  assert_true(first_span && second_span,
        "SpanProcessorPipeline bridge wiring test requires concrete SpanImpl instances from TracerImpl");

  (void)first_span->end(2500);
  (void)pipeline.on_end(first_span);
  (void)second_span->end(2501);
  (void)pipeline.on_end(second_span);

  const auto timeout_status = pipeline.force_flush(0U);
  const auto degraded_snapshot = pipeline.health_snapshot();
    assert_true(!timeout_status.ok,
          "SpanProcessorPipeline should surface the second forced failure before degraded-mode auditing is checked");
    assert_true(degraded_snapshot.degraded_mode,
          "SpanProcessorPipeline health snapshot should enter degraded mode after two consecutive failures");
    assert_true(has_audit_action(logger->events, "tracing.enter_degraded"),
        "Two consecutive pipeline failures should drive TraceHealthProbe into degraded mode and emit the corresponding TraceAuditBridge event");

      const auto recovery_span =
        std::dynamic_pointer_cast<SpanImpl>(tracer.start_span(make_descriptor("trace.pipeline.audit.recovery"), nullptr));
      assert_true(static_cast<bool>(recovery_span),
            "SpanProcessorPipeline recovery path requires a concrete recovery SpanImpl instance");
      (void)recovery_span->end(2502);
      (void)pipeline.on_end(recovery_span);

  const auto flush_status = pipeline.force_flush(250U);
  const auto recovered_snapshot = pipeline.health_snapshot();
  const auto recovery_context =
      find_audit_context(logger->events, logger->contexts, "tracing.recover_to_healthy");
    assert_true(flush_status.ok,
          "SpanProcessorPipeline should allow recovery via a successful force_flush() after degraded mode");
    assert_true(!recovered_snapshot.degraded_mode,
          "SpanProcessorPipeline health snapshot should clear degraded mode after recovery");
    assert_true(has_audit_action(logger->events, "tracing.recover_to_healthy"),
          "SpanProcessorPipeline should emit a recover_to_healthy audit event after a healthy force_flush() observation");
    assert_true(recovery_context.has_value(),
          "SpanProcessorPipeline recovery audit should be discoverable in the recording audit logger");
    assert_true(has_metric_sample(meter->samples,
                  "trace_export_failure_total",
                  "export",
                  "degraded",
                  std::string_view("TRC_E_EXPORT_TIMEOUT")),
          "SpanProcessorPipeline should retain the earlier export timeout metric emission while later recovery succeeds");
    assert_true(recovery_context->trace_id !=
            dasall::infra::InfraContext::kUnknownIdentifier,
        "Recovering the pipeline after degraded mode should emit the recovery audit event, preserve the last active trace_id, and retain the earlier export failure metrics emitted through TraceMetricsBridge");
}

}  // namespace

int main() {
  try {
    test_tracer_provider_force_flush_exports_buffered_spans();
    test_tracer_provider_immediately_exports_file_batches_when_batch_is_disabled();
    test_tracer_provider_surfaces_export_timeout_observably();
    test_tracer_provider_surfaces_unsupported_exporter_failure();
    test_tracer_provider_wires_export_metrics_into_trace_metrics_bridge();
    test_span_processor_pipeline_wires_health_transition_audit_into_trace_audit_bridge();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}