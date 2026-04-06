#include "tracing/SpanExporterAdapter.h"

#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "tracing/SpanImpl.h"

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kSpanExporterAdapterSourceRef = "SpanExporterAdapter";

[[nodiscard]] TraceOperationStatus make_exporter_failure(TraceErrorCode code,
                                                         std::string message,
                                                         std::string stage) {
  const auto mapping = map_trace_error_code(code);
  return TraceOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kSpanExporterAdapterSourceRef) + ":" +
          std::string(trace_error_code_name(code)));
}

[[nodiscard]] std::string span_status_name(SpanStatusCode code) {
  switch (code) {
    case SpanStatusCode::Unset:
      return "unset";
    case SpanStatusCode::Ok:
      return "ok";
    case SpanStatusCode::Error:
      return "error";
  }

  return "unknown";
}

}  // namespace

SpanExporterAdapter::SpanExporterAdapter()
    : SpanExporterAdapter(TraceConfig{}) {}

SpanExporterAdapter::SpanExporterAdapter(TraceConfig config)
    : config_(config.is_valid() ? std::move(config) : TraceConfig{}) {
  module_snapshot_.exporter_state = config_.exporter.type;
}

TraceOperationStatus SpanExporterAdapter::export_batch(const SpanBatch& batch) {
  if (!batch_is_exportable(batch)) {
    return invalid_request(TraceErrorCode::ConfigInvalid,
                           "trace exporter adapter requires a non-empty batch of ended spans",
                           "tracing.exporter.export_batch");
  }

  module_snapshot_.exporter_state = config_.exporter.type;
  if (config_.exporter.type == kTraceExporterTypeNoop) {
    return export_noop(batch);
  }

  if (config_.exporter.type == kTraceExporterTypeFile) {
    return export_file(batch);
  }

  return export_unsupported(batch);
}

TraceOperationStatus SpanExporterAdapter::force_flush(std::uint32_t timeout_ms) {
  if (timeout_ms == 0U) {
    return invalid_request(TraceErrorCode::ConfigInvalid,
                           "trace exporter adapter requires a positive flush deadline",
                           "tracing.exporter.force_flush");
  }

  return TraceOperationStatus::success("trace-exporter://flushed");
}

TraceOperationStatus SpanExporterAdapter::shutdown(std::uint32_t timeout_ms) {
  if (timeout_ms == 0U) {
    return invalid_request(TraceErrorCode::ConfigInvalid,
                           "trace exporter adapter requires a positive shutdown deadline",
                           "tracing.exporter.shutdown");
  }

  module_snapshot_.exporter_state = "stopped";
  return TraceOperationStatus::success("trace-exporter://stopped");
}

TraceOperationStatus SpanExporterAdapter::fallback_to_noop(std::string reason) {
  config_.exporter.type = std::string(kTraceExporterTypeNoop);
  module_snapshot_.exporter_state = std::string(kTraceExporterTypeNoop);
  module_snapshot_.degraded = true;
  last_rendered_output_.clear();
  return TraceOperationStatus::success("trace-exporter://fallback-noop:" +
                                       std::move(reason));
}

const ExportBatchReport& SpanExporterAdapter::last_report() const {
  return last_report_;
}

const TraceModuleSnapshot& SpanExporterAdapter::module_snapshot() const {
  return module_snapshot_;
}

std::uint64_t SpanExporterAdapter::export_success_total() const {
  return export_success_total_;
}

std::uint64_t SpanExporterAdapter::export_failure_total() const {
  return export_failure_total_;
}

const std::string& SpanExporterAdapter::last_rendered_output() const {
  return last_rendered_output_;
}

TraceOperationStatus SpanExporterAdapter::export_noop(const SpanBatch& batch) {
  export_success_total_ += static_cast<std::uint64_t>(batch.size());
  last_report_ = ExportBatchReport{
      .batch_size = static_cast<std::uint32_t>(batch.size()),
      .success_count = static_cast<std::uint32_t>(batch.size()),
      .failure_count = 0U,
      .latency_ms = 0U,
  };
  module_snapshot_.degraded = false;
  module_snapshot_.exporter_state = std::string(kTraceExporterTypeNoop);
  last_rendered_output_.clear();
  return TraceOperationStatus::success("trace-exporter://noop");
}

TraceOperationStatus SpanExporterAdapter::export_file(const SpanBatch& batch) {
  const auto latency_ms = simulated_latency_ms(batch);
  if (latency_ms > config_.export_timeout_ms) {
    export_failure_total_ += static_cast<std::uint64_t>(batch.size());
    last_report_ = ExportBatchReport{
        .batch_size = static_cast<std::uint32_t>(batch.size()),
        .success_count = 0U,
        .failure_count = static_cast<std::uint32_t>(batch.size()),
        .latency_ms = latency_ms,
    };
    (void)fallback_to_noop("timeout");
    return invalid_request(TraceErrorCode::ExportTimeout,
                           "trace exporter adapter timed out before file export completed",
                           "tracing.exporter.export_batch");
  }

  export_success_total_ += static_cast<std::uint64_t>(batch.size());
  last_report_ = ExportBatchReport{
      .batch_size = static_cast<std::uint32_t>(batch.size()),
      .success_count = static_cast<std::uint32_t>(batch.size()),
      .failure_count = 0U,
      .latency_ms = latency_ms,
  };
  module_snapshot_.degraded = false;
  module_snapshot_.exporter_state = std::string(kTraceExporterTypeFile);
  last_rendered_output_.clear();
  for (const auto& span : batch) {
    last_rendered_output_ += render_span_line(*span);
    last_rendered_output_ += '\n';
  }

  return TraceOperationStatus::success("trace-exporter://file");
}

TraceOperationStatus SpanExporterAdapter::export_unsupported(const SpanBatch& batch) {
  export_failure_total_ += static_cast<std::uint64_t>(batch.size());
  last_report_ = ExportBatchReport{
      .batch_size = static_cast<std::uint32_t>(batch.size()),
      .success_count = 0U,
      .failure_count = static_cast<std::uint32_t>(batch.size()),
      .latency_ms = 0U,
  };
  (void)fallback_to_noop("unsupported-exporter");
  return invalid_request(
      TraceErrorCode::ExportFailure,
      "trace exporter adapter only supports noop/file during the first export skeleton round",
      "tracing.exporter.export_batch");
}

TraceOperationStatus SpanExporterAdapter::invalid_request(TraceErrorCode code,
                                                          std::string message,
                                                          std::string stage) const {
  return make_exporter_failure(code, std::move(message), std::move(stage));
}

std::uint32_t SpanExporterAdapter::simulated_latency_ms(const SpanBatch& batch) const {
  return static_cast<std::uint32_t>(batch.size());
}

bool SpanExporterAdapter::batch_is_exportable(const SpanBatch& batch) {
  if (batch.empty()) {
    return false;
  }

  for (const auto& span : batch) {
    if (!span || !span->has_ended()) {
      return false;
    }
  }

  return true;
}

std::string SpanExporterAdapter::render_span_line(const SpanImpl& span) {
  std::ostringstream stream;
  const auto context = span.get_context();
  const auto& end_result = span.end_result();
  stream << "trace_id=" << context.trace_id
         << " span_id=" << context.span_id
         << " name=" << span.descriptor().name
         << " status=" << span_status_name(end_result.status_code)
         << " end_ts=" << end_result.end_ts_unix_ms.value_or(-1)
         << " attrs=" << span.attributes().size();
  return stream.str();
}

}  // namespace dasall::infra::tracing