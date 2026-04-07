#include "tracing/SpanProcessorPipeline.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kSpanProcessorPipelineSourceRef = "SpanProcessorPipeline";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string trace_metric_outcome_name(bool ok, bool degraded) {
  if (degraded) {
    return "degraded";
  }

  return ok ? "success" : "failure";
}

}  // namespace

SpanProcessorPipeline::SpanProcessorPipeline(TraceConfig config)
    : config_(config.is_valid() ? std::move(config) : TraceConfig{}),
      buffer_(config_),
      exporter_(config_),
      audit_bridge_(nullptr,
                    TraceAuditBridgeOptions{
                        .detail_ref_prefix = "status://tracing/pipeline/audit/",
                        .event_id_prefix = "trace-pipeline-audit-event-",
                    }) {
  refresh_snapshot();
}

void SpanProcessorPipeline::set_metrics_provider(
    std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
    std::string profile_id) {
  metrics_bridge_.set_metrics_provider(std::move(metrics_provider),
                                       std::move(profile_id));
}

void SpanProcessorPipeline::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    TraceAuditContext audit_context) {
  audit_bridge_.set_audit_logger(std::move(audit_logger));
  audit_context_ = audit_context.is_valid() ? std::move(audit_context)
                                            : TraceAuditContext{};
}

TraceOperationStatus SpanProcessorPipeline::on_end(const std::shared_ptr<SpanImpl>& span) {
  const auto timestamp_ms = span && span->end_result().end_ts_unix_ms.has_value()
                                ? *span->end_result().end_ts_unix_ms
                                : current_time_unix_ms();
  if (!span || !span->has_ended()) {
    last_status_ = make_failure(TraceErrorCode::ConfigInvalid,
                                "span processor pipeline requires an ended span",
                                "tracing.pipeline.on_end");
    refresh_snapshot();
    emit_queue_depth_metric(timestamp_ms);
    observe_health_state(timestamp_ms);
    return last_status_;
  }

  if (span->get_context().state == TraceContextState::Active &&
      span->get_context().is_valid()) {
    last_trace_id_ = span->get_context().trace_id;
  }
  emit_span_ended_metric(*span);

  if (!span->is_recording()) {
    ++ignored_span_total_;
    last_status_ = TraceOperationStatus::success("trace-pipeline://ignored-non-recording");
    refresh_snapshot();
    emit_queue_depth_metric(timestamp_ms);
    observe_health_state(timestamp_ms);
    return last_status_;
  }

  ++processed_span_total_;
  const auto enqueue_result = buffer_.enqueue(span);
  refresh_snapshot();
  emit_span_dropped_metric(enqueue_result, timestamp_ms);
  emit_queue_depth_metric(timestamp_ms);
  if (!enqueue_result.status.ok) {
    last_status_ = enqueue_result.status;
    observe_health_state(timestamp_ms);
    return last_status_;
  }

  const auto now_unix_ms = span->end_result().end_ts_unix_ms.value_or(0);
  if (enqueue_result.export_requested || buffer_.should_export_now(now_unix_ms)) {
    return export_batch(buffer_.dequeue_batch());
  }

  last_status_ = TraceOperationStatus::success("trace-pipeline://buffered");
  observe_health_state(timestamp_ms);
  return last_status_;
}

TraceOperationStatus SpanProcessorPipeline::force_flush(std::uint32_t timeout_ms) {
  const auto timestamp_ms = current_time_unix_ms();
  if (timeout_ms == 0U) {
    last_status_ = make_failure(TraceErrorCode::ExportTimeout,
                                "span processor pipeline force_flush() timed out before export completed",
                                "tracing.pipeline.force_flush");
    refresh_snapshot();
    emit_queue_depth_metric(timestamp_ms);
    observe_health_state(timestamp_ms);
    return last_status_;
  }

  last_status_ = flush_pending_buffer();
  if (!last_status_.ok) {
    return last_status_;
  }

  last_status_ = exporter_.force_flush(timeout_ms);
  refresh_snapshot();
  emit_queue_depth_metric(timestamp_ms);
  observe_health_state(timestamp_ms);
  return last_status_;
}

TraceOperationStatus SpanProcessorPipeline::shutdown(std::uint32_t timeout_ms,
                                                     bool force_flush_on_stop) {
  const auto timestamp_ms = current_time_unix_ms();
  if (timeout_ms == 0U) {
    last_status_ = make_failure(TraceErrorCode::ShutdownTimeout,
                                "span processor pipeline shutdown() timed out before exporter stopped",
                                "tracing.pipeline.shutdown");
    refresh_snapshot();
    emit_queue_depth_metric(timestamp_ms);
    observe_health_state(timestamp_ms);
    emit_shutdown_fallback_audit(last_status_, timestamp_ms);
    return last_status_;
  }

  if (force_flush_on_stop) {
    const auto flush_status = force_flush(timeout_ms);
    if (!flush_status.ok) {
      (void)exporter_.shutdown(timeout_ms);
      refresh_snapshot();
      last_status_ = flush_status;
      emit_queue_depth_metric(timestamp_ms);
      observe_health_state(timestamp_ms);
      emit_shutdown_fallback_audit(last_status_, timestamp_ms);
      return last_status_;
    }
  }

  last_status_ = exporter_.shutdown(timeout_ms);
  refresh_snapshot();
  emit_queue_depth_metric(timestamp_ms);
  observe_health_state(timestamp_ms);
  if (!last_status_.ok) {
    emit_shutdown_fallback_audit(last_status_, timestamp_ms);
  }
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
  const auto timestamp_ms = !batch.empty() &&
                                    batch.back()->end_result().end_ts_unix_ms.has_value()
                                ? *batch.back()->end_result().end_ts_unix_ms
                                : current_time_unix_ms();
  if (batch.empty()) {
    last_status_ = TraceOperationStatus::success("trace-pipeline://idle");
    refresh_snapshot();
    emit_queue_depth_metric(timestamp_ms);
    return last_status_;
  }

  if (batch.back()->get_context().state == TraceContextState::Active &&
      batch.back()->get_context().is_valid()) {
    last_trace_id_ = batch.back()->get_context().trace_id;
  }

  last_status_ = exporter_.export_batch(batch);
  if (last_status_.ok) {
    buffer_.mark_export_cycle_complete(batch.back()->end_result().end_ts_unix_ms.value_or(0));
  }
  refresh_snapshot();
  emit_export_metrics(exporter_.last_report(), timestamp_ms);
  emit_queue_depth_metric(timestamp_ms);
  observe_health_state(timestamp_ms);
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
  const auto timestamp_ms = current_time_unix_ms();
  emit_queue_depth_metric(timestamp_ms);
  observe_health_state(timestamp_ms);
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

std::optional<TraceErrorCode> SpanProcessorPipeline::infer_trace_error_code(
    const TraceOperationStatus& status) {
  if (!status.error.has_value()) {
    return std::nullopt;
  }

  const auto& source_ref = status.error->source_ref.ref_id;
  for (const auto code : {TraceErrorCode::ProviderNotReady,
                          TraceErrorCode::InvalidContext,
                          TraceErrorCode::QueueFull,
                          TraceErrorCode::ExportTimeout,
                          TraceErrorCode::ExportFailure,
                          TraceErrorCode::ShutdownTimeout,
                          TraceErrorCode::ConfigInvalid}) {
    if (source_ref.find(trace_error_code_name(code)) != std::string::npos) {
      return code;
    }
  }

  return std::nullopt;
}

TraceAuditContext SpanProcessorPipeline::make_audit_context(std::string trace_id) const {
  auto context = audit_context_;
  if (!trace_id.empty()) {
    context.infra_context.trace_id = std::move(trace_id);
  }

  return context;
}

void SpanProcessorPipeline::emit_span_ended_metric(const SpanImpl& span) {
  if (!metrics_bridge_.has_metrics_provider()) {
    return;
  }

  (void)metrics_bridge_.emit(TraceMetricSignal{
      .kind = TraceMetricKind::SpanEndedTotal,
      .value = 1.0,
      .ts_unix_ms = span.end_result().end_ts_unix_ms.value_or(current_time_unix_ms()),
      .stage = std::string("span"),
      .outcome = std::string("success"),
      .trace_error_code = std::nullopt,
  });
}

void SpanProcessorPipeline::emit_span_dropped_metric(
    const BatchSpanEnqueueResult& enqueue_result,
    std::int64_t timestamp_ms) {
  if (!metrics_bridge_.has_metrics_provider()) {
    return;
  }

  if (enqueue_result.dropped_oldest) {
    (void)metrics_bridge_.emit(TraceMetricSignal{
        .kind = TraceMetricKind::SpanDroppedTotal,
        .value = 1.0,
        .ts_unix_ms = timestamp_ms,
        .stage = std::string("queue"),
        .outcome = std::string("degraded"),
        .trace_error_code = TraceErrorCode::QueueFull,
    });
  }

  if (enqueue_result.would_block) {
    (void)metrics_bridge_.emit(TraceMetricSignal{
        .kind = TraceMetricKind::SpanDroppedTotal,
        .value = 1.0,
        .ts_unix_ms = timestamp_ms,
        .stage = std::string("queue"),
        .outcome = std::string("failure"),
        .trace_error_code = TraceErrorCode::QueueFull,
    });
  }
}

void SpanProcessorPipeline::emit_queue_depth_metric(std::int64_t timestamp_ms) {
  if (!metrics_bridge_.has_metrics_provider()) {
    return;
  }

  (void)metrics_bridge_.emit(TraceMetricSignal{
      .kind = TraceMetricKind::BatchQueueDepth,
      .value = static_cast<double>(module_snapshot_.queue_depth),
      .ts_unix_ms = timestamp_ms,
      .stage = std::string("queue"),
      .outcome = trace_metric_outcome_name(last_status_.ok, module_snapshot_.degraded),
      .trace_error_code = std::nullopt,
  });
}

void SpanProcessorPipeline::emit_export_metrics(const ExportBatchReport& report,
                                                std::int64_t timestamp_ms) {
  if (!metrics_bridge_.has_metrics_provider() || report.batch_size == 0U) {
    return;
  }

  const auto error_code = infer_trace_error_code(last_status_).value_or(
      module_snapshot_.degraded ? TraceErrorCode::ExportFailure
                                : TraceErrorCode::ConfigInvalid);
  const auto outcome = trace_metric_outcome_name(last_status_.ok, module_snapshot_.degraded);

  if (report.success_count > 0U) {
    (void)metrics_bridge_.emit(TraceMetricSignal{
        .kind = TraceMetricKind::ExportSuccessTotal,
        .value = static_cast<double>(report.success_count),
        .ts_unix_ms = timestamp_ms,
        .stage = std::string("export"),
        .outcome = std::string("success"),
        .trace_error_code = std::nullopt,
    });
  }

  if (report.failure_count > 0U) {
    (void)metrics_bridge_.emit(TraceMetricSignal{
        .kind = TraceMetricKind::ExportFailureTotal,
        .value = static_cast<double>(report.failure_count),
        .ts_unix_ms = timestamp_ms,
        .stage = std::string("export"),
        .outcome = outcome,
        .trace_error_code = error_code,
    });
  }

  (void)metrics_bridge_.emit(TraceMetricSignal{
      .kind = TraceMetricKind::ExportLatencyMs,
      .value = static_cast<double>(report.latency_ms),
      .ts_unix_ms = timestamp_ms,
      .stage = std::string("export"),
      .outcome = outcome,
      .trace_error_code = last_status_.ok ? std::nullopt
                                          : std::optional<TraceErrorCode>(error_code),
  });
}

void SpanProcessorPipeline::emit_export_recovery_audit(
    const TraceHealthSnapshot& previous_snapshot,
    const TraceHealthSnapshot& current_snapshot,
    std::int64_t timestamp_ms) {
  if (!audit_bridge_.has_audit_logger()) {
    return;
  }

  std::string action;
  TraceAuditEventOutcome outcome = TraceAuditEventOutcome::Success;
  std::optional<TraceErrorCode> error_code = current_snapshot.last_error_code;
  std::string reason = current_snapshot.last_failure_reason;

  if (!previous_snapshot.degraded_mode && current_snapshot.degraded_mode) {
    action = "enter_degraded";
    outcome = TraceAuditEventOutcome::Degraded;
  } else if (previous_snapshot.degraded_mode && current_snapshot.degraded_mode &&
             current_snapshot.consecutive_failure_total >
                 previous_snapshot.consecutive_failure_total) {
    action = "degraded_still_active";
    outcome = TraceAuditEventOutcome::Degraded;
  } else if (previous_snapshot.degraded_mode && !current_snapshot.degraded_mode) {
    action = "recover_to_healthy";
    outcome = TraceAuditEventOutcome::Success;
    error_code.reset();
    reason = "tracing exporter recovered and health state returned to healthy";
  } else {
    return;
  }

  (void)audit_bridge_.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::ExportRecoveryTransition,
      .action = std::move(action),
      .stage = std::string("tracing.health.observe_result"),
      .outcome = outcome,
      .reason = reason.empty() ? std::string("tracing health state changed") : std::move(reason),
      .error_code = error_code,
      .module_snapshot = current_snapshot.module_snapshot,
      .context = make_audit_context(last_trace_id_),
      .detail_ref = current_snapshot.detail_ref.empty()
                        ? std::string("status://tracing/health/state")
                        : current_snapshot.detail_ref,
      .current_sampler_type = std::string(),
      .previous_sampler_type = std::string(),
      .consecutive_failure_total = current_snapshot.consecutive_failure_total,
      .degrade_enter_total = current_snapshot.degrade_enter_total,
      .recovery_success_total = current_snapshot.recovery_success_total,
      .timestamp_ms = timestamp_ms,
  });
}

void SpanProcessorPipeline::emit_shutdown_fallback_audit(
    const TraceOperationStatus& status,
    std::int64_t timestamp_ms) {
  if (!audit_bridge_.has_audit_logger()) {
    return;
  }

  const auto error_code = infer_trace_error_code(status).value_or(
      TraceErrorCode::ShutdownTimeout);
  const auto degraded = module_snapshot_.degraded;

  (void)audit_bridge_.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::ShutdownFallback,
      .action = std::string("shutdown_force_fallback"),
      .stage = status.error.has_value() ? status.error->details.stage
                                        : std::string("tracing.pipeline.shutdown"),
      .outcome = degraded ? TraceAuditEventOutcome::Degraded
                          : TraceAuditEventOutcome::Failure,
      .reason = status.error.has_value()
                    ? status.error->details.message
                    : std::string(
                          "tracing pipeline entered shutdown fallback handling after a failed stop request"),
      .error_code = error_code,
      .module_snapshot = module_snapshot_,
      .context = make_audit_context(last_trace_id_),
      .detail_ref = std::string("status://tracing/shutdown/fallback"),
      .current_sampler_type = std::string(),
      .previous_sampler_type = std::string(),
      .timestamp_ms = timestamp_ms,
  });
}

void SpanProcessorPipeline::refresh_snapshot() {
  module_snapshot_.queue_depth = static_cast<std::uint32_t>(buffer_.queue_depth());
  module_snapshot_.dropped_total = buffer_.dropped_total();
  module_snapshot_.exporter_state = exporter_.module_snapshot().exporter_state;
  module_snapshot_.degraded = exporter_.module_snapshot().degraded;
}

void SpanProcessorPipeline::observe_health_state(std::int64_t timestamp_ms) {
  const auto previous_snapshot = health_probe_.snapshot();
  (void)health_probe_.observe_result(last_status_, module_snapshot_);
  emit_export_recovery_audit(previous_snapshot, health_probe_.snapshot(), timestamp_ms);
}

}  // namespace dasall::infra::tracing