#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "tracing/TraceConfig.h"
#include "tracing/TraceErrors.h"
#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

class SpanImpl;

class SpanExporterAdapter {
 public:
  using SpanBatch = std::vector<std::shared_ptr<SpanImpl>>;

  SpanExporterAdapter();
  explicit SpanExporterAdapter(TraceConfig config);

  TraceOperationStatus export_batch(const SpanBatch& batch);
  TraceOperationStatus force_flush(std::uint32_t timeout_ms);
  TraceOperationStatus shutdown(std::uint32_t timeout_ms);

  TraceOperationStatus fallback_to_noop(std::string reason);
  [[nodiscard]] const ExportBatchReport& last_report() const;
  [[nodiscard]] const TraceModuleSnapshot& module_snapshot() const;
  [[nodiscard]] std::uint64_t export_success_total() const;
  [[nodiscard]] std::uint64_t export_failure_total() const;
  [[nodiscard]] const std::string& last_rendered_output() const;

 private:
  TraceOperationStatus export_noop(const SpanBatch& batch);
  TraceOperationStatus export_file(const SpanBatch& batch);
  TraceOperationStatus export_unsupported(const SpanBatch& batch);
  TraceOperationStatus invalid_request(TraceErrorCode code,
                                       std::string message,
                                       std::string stage) const;
  [[nodiscard]] std::uint32_t simulated_latency_ms(const SpanBatch& batch) const;
  [[nodiscard]] static bool batch_is_exportable(const SpanBatch& batch);
  [[nodiscard]] static std::string render_span_line(const SpanImpl& span);

  TraceConfig config_;
  ExportBatchReport last_report_{};
  TraceModuleSnapshot module_snapshot_{.exporter_state = "unknown"};
  std::uint64_t export_success_total_ = 0;
  std::uint64_t export_failure_total_ = 0;
  std::string last_rendered_output_;
};

}  // namespace dasall::infra::tracing