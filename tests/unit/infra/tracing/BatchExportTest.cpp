#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "tracing/ISpan.h"
#include "tracing/ITracer.h"
#include "tracing/TraceErrors.h"
#include "tracing/TracerProviderImpl.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

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
                                                              std::string otlp_endpoint = {}) {
  dasall::infra::tracing::TraceConfig config;
  config.exporter.type = std::move(exporter_type);
  config.exporter.otlp_endpoint = std::move(otlp_endpoint);
  config.batch.enabled = batch_enabled;
  config.batch.max_queue_size = 8U;
  config.batch.max_export_batch_size = max_export_batch_size;
  config.batch.schedule_delay_ms = 500U;
  config.export_timeout_ms = export_timeout_ms;
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

}  // namespace

int main() {
  try {
    test_tracer_provider_force_flush_exports_buffered_spans();
    test_tracer_provider_immediately_exports_file_batches_when_batch_is_disabled();
    test_tracer_provider_surfaces_export_timeout_observably();
    test_tracer_provider_surfaces_unsupported_exporter_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}