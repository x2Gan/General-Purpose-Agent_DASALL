#include "tracing/SpanProcessorPipeline.h"

#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kSpanProcessorPipelineSourceRef = "SpanProcessorPipeline";

}  // namespace

SpanProcessorPipeline::SpanProcessorPipeline(TraceConfig config)
    : config_(config.is_valid() ? std::move(config) : TraceConfig{}),
      buffer_(config_),
      exporter_(config_) {
  refresh_snapshot();
}

TraceOperationStatus SpanProcessorPipeline::on_end(const std::shared_ptr<SpanImpl>& span) {
  if (!span || !span->has_ended()) {
    last_status_ = make_failure(TraceErrorCode::ConfigInvalid,
                                "span processor pipeline requires an ended span",
                                "tracing.pipeline.on_end");
    refresh_snapshot();
    observe_health_state();
    return last_status_;
  }

  if (!span->is_recording()) {
    ++ignored_span_total_;
    last_status_ = TraceOperationStatus::success("trace-pipeline://ignored-non-recording");
    refresh_snapshot();
    observe_health_state();
    return last_status_;
  }

  ++processed_span_total_;
  const auto enqueue_result = buffer_.enqueue(span);
  refresh_snapshot();
  if (!enqueue_result.status.ok) {
    last_status_ = enqueue_result.status;
    observe_health_state();
    return last_status_;
  }

  const auto now_unix_ms = span->end_result().end_ts_unix_ms.value_or(0);
  if (enqueue_result.export_requested || buffer_.should_export_now(now_unix_ms)) {
    return export_batch(buffer_.dequeue_batch());
  }

  last_status_ = TraceOperationStatus::success("trace-pipeline://buffered");
  observe_health_state();
  return last_status_;
}

TraceOperationStatus SpanProcessorPipeline::force_flush(std::uint32_t timeout_ms) {
  if (timeout_ms == 0U) {
    last_status_ = make_failure(TraceErrorCode::ExportTimeout,
                                "span processor pipeline force_flush() timed out before export completed",
                                "tracing.pipeline.force_flush");
    refresh_snapshot();
    observe_health_state();
    return last_status_;
  }

  last_status_ = flush_pending_buffer();
  if (!last_status_.ok) {
    return last_status_;
  }

  last_status_ = exporter_.force_flush(timeout_ms);
  refresh_snapshot();
  observe_health_state();
  return last_status_;
}

TraceOperationStatus SpanProcessorPipeline::shutdown(std::uint32_t timeout_ms,
                                                     bool force_flush_on_stop) {
  if (timeout_ms == 0U) {
    last_status_ = make_failure(TraceErrorCode::ShutdownTimeout,
                                "span processor pipeline shutdown() timed out before exporter stopped",
                                "tracing.pipeline.shutdown");
    refresh_snapshot();
    observe_health_state();
    return last_status_;
  }

  if (force_flush_on_stop) {
    const auto flush_status = force_flush(timeout_ms);
    if (!flush_status.ok) {
      (void)exporter_.shutdown(timeout_ms);
      refresh_snapshot();
      last_status_ = flush_status;
      observe_health_state();
      return last_status_;
    }
  }

  last_status_ = exporter_.shutdown(timeout_ms);
  refresh_snapshot();
  observe_health_state();
  return last_status_;
}

const BatchSpanBuffer& SpanProcessorPipeline::buffer() const {
  return buffer_;
}

const SpanExporterAdapter& SpanProcessorPipeline::exporter() const {
  return exporter_;
}

const TraceHealthProbe& SpanProcessorPipeline::health_probe() const {
  return health_probe_;
}

const TraceHealthSnapshot& SpanProcessorPipeline::health_snapshot() const {
  return health_probe_.snapshot();
}

const TraceOperationStatus& SpanProcessorPipeline::last_status() const {
  return last_status_;
}

const TraceModuleSnapshot& SpanProcessorPipeline::module_snapshot() const {
  return module_snapshot_;
}

std::uint64_t SpanProcessorPipeline::processed_span_total() const {
  return processed_span_total_;
}

std::uint64_t SpanProcessorPipeline::ignored_span_total() const {
  return ignored_span_total_;
}

TraceOperationStatus SpanProcessorPipeline::export_batch(SpanExporterAdapter::SpanBatch batch) {
  if (batch.empty()) {
    last_status_ = TraceOperationStatus::success("trace-pipeline://idle");
    refresh_snapshot();
    return last_status_;
  }

  last_status_ = exporter_.export_batch(batch);
  if (last_status_.ok) {
    buffer_.mark_export_cycle_complete(batch.back()->end_result().end_ts_unix_ms.value_or(0));
  }
  refresh_snapshot();
  observe_health_state();
  return last_status_;
}

TraceOperationStatus SpanProcessorPipeline::flush_pending_buffer() {
  while (buffer_.queue_depth() > 0U) {
    const auto status = export_batch(buffer_.dequeue_batch());
    if (!status.ok) {
      return status;
    }
  }

  last_status_ = TraceOperationStatus::success("trace-pipeline://drained");
  refresh_snapshot();
  observe_health_state();
  return last_status_;
}

TraceOperationStatus SpanProcessorPipeline::make_failure(TraceErrorCode code,
                                                         std::string message,
                                                         std::string stage) {
  const auto mapping = map_trace_error_code(code);
  return TraceOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kSpanProcessorPipelineSourceRef) + ":" +
          std::string(trace_error_code_name(code)));
}

void SpanProcessorPipeline::refresh_snapshot() {
  module_snapshot_.queue_depth = static_cast<std::uint32_t>(buffer_.queue_depth());
  module_snapshot_.dropped_total = buffer_.dropped_total();
  module_snapshot_.exporter_state = exporter_.module_snapshot().exporter_state;
  module_snapshot_.degraded = exporter_.module_snapshot().degraded;
}

void SpanProcessorPipeline::observe_health_state() {
  (void)health_probe_.observe_result(last_status_, module_snapshot_);
}

}  // namespace dasall::infra::tracing