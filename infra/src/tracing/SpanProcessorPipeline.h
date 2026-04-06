#pragma once

#include <cstdint>
#include <memory>

#include "tracing/BatchSpanBuffer.h"
#include "tracing/SpanExporterAdapter.h"

namespace dasall::infra::tracing {

class SpanProcessorPipeline {
 public:
  explicit SpanProcessorPipeline(TraceConfig config = {});

  TraceOperationStatus on_end(const std::shared_ptr<SpanImpl>& span);
  TraceOperationStatus force_flush(std::uint32_t timeout_ms);
  TraceOperationStatus shutdown(std::uint32_t timeout_ms, bool force_flush_on_stop = true);

  [[nodiscard]] const BatchSpanBuffer& buffer() const;
  [[nodiscard]] const SpanExporterAdapter& exporter() const;
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
  void refresh_snapshot();

  TraceConfig config_;
  BatchSpanBuffer buffer_;
  SpanExporterAdapter exporter_;
  TraceOperationStatus last_status_ = TraceOperationStatus::success("trace-pipeline://idle");
  TraceModuleSnapshot module_snapshot_{.exporter_state = "uninitialized"};
  std::uint64_t processed_span_total_ = 0;
  std::uint64_t ignored_span_total_ = 0;
};

}  // namespace dasall::infra::tracing