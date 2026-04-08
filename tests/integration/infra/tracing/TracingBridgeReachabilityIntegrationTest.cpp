#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audit/IAuditLogger.h"
#include "metrics/IMeter.h"
#include "metrics/MetricTypes.h"
#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/TraceConfig.h"
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
        "metrics://tracing-integration-record");
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
        "metrics://tracing-integration-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://tracing-integration-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://tracing-integration-provider-shutdown");
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
    const std::string_view& name,
    const std::string_view& stage,
    const std::string_view& outcome,
    const std::optional<std::string_view>& error_code = std::nullopt) {
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
    const std::string_view& action) {
  return std::any_of(events.begin(), events.end(), [&](const auto& event) {
    return event.action == action;
  });
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string_view& expected) {
  return std::any_of(event.side_effects.begin(),
                     event.side_effects.end(),
                     [&](const std::string& side_effect) {
                       return side_effect == expected;
                     });
}

[[nodiscard]] std::optional<dasall::infra::AuditContext> find_audit_context(
    const std::vector<dasall::infra::AuditEvent>& events,
    const std::vector<dasall::infra::AuditContext>& contexts,
    const std::string_view& action) {
  for (std::size_t index = 0; index < events.size() && index < contexts.size(); ++index) {
    if (events[index].action == action) {
      return contexts[index];
    }
  }

  return std::nullopt;
}

[[nodiscard]] dasall::infra::InfraContext make_infra_context() {
  return dasall::infra::InfraContext{
      .request_id = std::string("req-tracing-integration-001"),
      .session_id = std::string("sess-tracing-integration-001"),
      .trace_id = std::string("trace-tracing-integration-001"),
      .task_id = std::string("task-tracing-integration-001"),
      .parent_task_id = std::string("parent-tracing-integration-001"),
      .lease_id = std::string("lease-tracing-integration-001"),
  };
}

[[nodiscard]] dasall::infra::tracing::TracerScope make_scope() {
  return dasall::infra::tracing::TracerScope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::tracing::SpanDescriptor make_descriptor(std::string name,
                                                                     std::int64_t start_ts) {
  return dasall::infra::tracing::SpanDescriptor{
      .name = std::move(name),
      .kind = dasall::infra::tracing::SpanKind::Internal,
      .start_ts_unix_ms = start_ts,
      .attrs = {{"component",
                 dasall::infra::tracing::TraceAttributeValue{std::string("integration")}}},
      .links = {},
  };
}

[[nodiscard]] dasall::infra::tracing::TraceConfig make_config() {
  dasall::infra::tracing::TraceConfig config;
  config.exporter.type = std::string(dasall::infra::tracing::kTraceExporterTypeOtlp);
  config.exporter.otlp_endpoint = std::string("http://127.0.0.1:4318");
  config.batch.enabled = false;
  config.batch.max_queue_size = 8U;
  config.batch.max_export_batch_size = 4U;
  config.batch.schedule_delay_ms = 500U;
  config.export_timeout_ms = 10U;
  config.force_flush_on_stop = true;
  return config;
}

void start_and_end_span(dasall::infra::tracing::TracerProviderImpl& provider,
                        std::string span_name,
                        std::int64_t start_ts_unix_ms,
                        std::int64_t end_ts_unix_ms) {
  const auto tracer = provider.get_tracer(make_scope());
  const auto span = tracer->start_span(
      make_descriptor(std::move(span_name), start_ts_unix_ms), nullptr);
  (void)span->end(end_ts_unix_ms);
}

void test_tracing_bridge_reachability_integration_wires_provider_pipeline_and_sinks() {
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TracerProviderImpl;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto logger = std::make_shared<RecordingAuditLogger>();

  TracerProviderImpl provider;
  provider.set_metrics_provider(metrics_provider, "desktop_full");
  provider.set_audit_logger(logger, make_infra_context());

  const auto init_result = provider.init(make_config());
  start_and_end_span(provider,
                     "trace.integration.bridge_reachability",
                     1712534400000,
                     1712534400001);
  const auto pipeline_status = provider.last_pipeline_status();
  const auto export_failure_total = provider.export_failure_total();
  const auto shutdown_timeout = provider.shutdown(0U);
  const auto shutdown_result = provider.shutdown(250U);

  assert_true(init_result.ok,
              "tracing integration reachability should initialize the provider before bridge assertions");
  assert_equal(std::string("infra.tracing"),
               metrics_provider->last_scope.name,
               "tracing integration reachability should request the frozen infra.tracing meter scope through the runtime provider chain");
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
              "tracing integration reachability should push ended-span, export-failure, latency, and queue-depth runtime signals through TraceMetricsBridge");

  assert_true(!pipeline_status.ok &&
                  pipeline_status.result_code ==
                      map_trace_error_code(TraceErrorCode::ExportFailure).result_code &&
                  export_failure_total == 1U,
              "tracing integration reachability should keep the exporter failure observable on the provider pipeline state");

  assert_true(!shutdown_timeout.ok && shutdown_result.ok,
              "tracing integration reachability should surface the shutdown timeout audit path before a later clean shutdown");
  assert_true(has_audit_action(logger->events, "tracing.sampler_changed") &&
                  has_audit_action(logger->events, "tracing.shutdown_force_fallback"),
              "tracing integration reachability should deliver provider lifecycle governance events into TraceAuditBridge");

  const auto shutdown_context =
      find_audit_context(logger->events, logger->contexts, "tracing.shutdown_force_fallback");
    const bool has_shutdown_timeout_side_effect =
      std::any_of(logger->events.begin(), logger->events.end(), [&](const auto& event) {
    return event.action == "tracing.shutdown_force_fallback" &&
         has_side_effect(event, "error_code:TRC_E_SHUTDOWN_TIMEOUT");
      });
  assert_true(shutdown_context.has_value(),
              "tracing integration reachability should preserve the shutdown fallback audit context for later inspection");
  assert_true(shutdown_context->request_id == "req-tracing-integration-001" &&
          has_shutdown_timeout_side_effect,
              "tracing integration reachability should keep provider correlation fields in AuditContext and the timeout token in the shutdown audit side_effects");
}

}  // namespace

int main() {
  try {
    test_tracing_bridge_reachability_integration_wires_provider_pipeline_and_sinks();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}