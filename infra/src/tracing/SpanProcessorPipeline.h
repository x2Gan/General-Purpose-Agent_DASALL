#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "tracing/TraceAuditBridge.h"
#include "tracing/BatchSpanBuffer.h"
#include "tracing/SpanExporterAdapter.h"
#include "tracing/TraceHealthProbe.h"
#include "tracing/TraceMetricsBridge.h"

namespace dasall::infra::tracing {

class SpanProcessorPipeline {
 public:
  explicit SpanProcessorPipeline(TraceConfig config = {});

  void set_metrics_provider(
      std::shared_ptr<metrics::IMetricsProvider> metrics_provider,
      std::string profile_id = "unknown");
  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger,
                        TraceAuditContext audit_context = {});

  TraceOperationStatus on_end(const std::shared_ptr<SpanImpl>& span);
  TraceOperationStatus force_flush(std::uint32_t timeout_ms);
  TraceOperationStatus shutdown(std::uint32_t timeout_ms, bool force_flush_on_stop = true);

  [[nodiscard]] const BatchSpanBuffer& buffer() const;
  [[nodiscard]] const SpanExporterAdapter& exporter() const;
  [[nodiscard]] const TraceHealthProbe& health_probe() const;
  [[nodiscard]] const TraceHealthSnapshot& health_snapshot() const;
  [[nodiscard]] const TraceOperationStatus& last_status() const;
  [[nodiscard]] const TraceModuleSnapshot& module_snapshot() const;
  [[nodiscard]] std::uint64_t processed_span_total() const;
  [[nodiscard]] std::uint64_t ignored_span_total() const;

 private:
  [[nodiscard]] TraceOperationStatus export_batch(SpanExporterAdapter::SpanBatch batch);
  [[nodiscard]] TraceOperationStatus flush_pending_buffer();
  [[nodiscard]] static TraceOperationStatus make_failure(TraceErrorCode code,
                                                         std::string message,
                                                         std::string stage);
  [[nodiscard]] static std::optional<TraceErrorCode> infer_trace_error_code(
      const TraceOperationStatus& status);
  [[nodiscard]] TraceAuditContext make_audit_context(
      std::string trace_id = std::string()) const;
  void emit_span_ended_metric(const SpanImpl& span);
  void emit_span_dropped_metric(const BatchSpanEnqueueResult& enqueue_result,
                                std::int64_t timestamp_ms);
  void emit_queue_depth_metric(std::int64_t timestamp_ms);
  void emit_export_metrics(const ExportBatchReport& report,
                           std::int64_t timestamp_ms);
  void emit_export_recovery_audit(const TraceHealthSnapshot& previous_snapshot,
                                  const TraceHealthSnapshot& current_snapshot,
                                  std::int64_t timestamp_ms);
  void emit_shutdown_fallback_audit(const TraceOperationStatus& status,
                                    std::int64_t timestamp_ms);
  void refresh_snapshot();
  void observe_health_state(std::int64_t timestamp_ms);

  TraceConfig config_;
  BatchSpanBuffer buffer_;
  SpanExporterAdapter exporter_;
  TraceHealthProbe health_probe_{};
  TraceMetricsBridge metrics_bridge_{nullptr};
  TraceAuditBridge audit_bridge_{};
  TraceAuditContext audit_context_{};
  TraceOperationStatus last_status_ = TraceOperationStatus::success("trace-pipeline://idle");
  TraceModuleSnapshot module_snapshot_{.exporter_state = "uninitialized"};
  std::uint64_t processed_span_total_ = 0;
  std::uint64_t ignored_span_total_ = 0;
  std::string last_trace_id_ = std::string(InfraContext::kUnknownIdentifier);
};

}  // namespace dasall::infra::tracing