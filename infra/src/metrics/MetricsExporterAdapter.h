#pragma once

#include <cstdint>
#include <string>

#include "metrics/IMetricExporter.h"
#include "metrics/MetricsConfigPolicy.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricsSnapshots.h"

namespace dasall::infra::metrics {

class MetricsExporterAdapter final : public IMetricExporter {
 public:
  MetricsExporterAdapter();
  explicit MetricsExporterAdapter(MetricsResolvedConfig config);

  MetricsOperationStatus export_batch(const MetricExportBatch& batch) override;
  MetricsOperationStatus force_flush(const MetricsCallDeadline& timeout) override;
  MetricsOperationStatus shutdown(const MetricsCallDeadline& timeout) override;

  MetricsOperationStatus fallback_to_noop(std::string reason);
  [[nodiscard]] const ExportBatchReport& last_report() const;
  [[nodiscard]] const MetricsModuleSnapshot& module_snapshot() const;
  [[nodiscard]] std::uint64_t export_success_total() const;
  [[nodiscard]] std::uint64_t export_failure_total() const;
  [[nodiscard]] const std::string& last_rendered_text() const;

 private:
  MetricsOperationStatus export_noop(const MetricExportBatch& batch);
  MetricsOperationStatus export_prom_text(const MetricExportBatch& batch);
  MetricsOperationStatus export_unsupported(const MetricExportBatch& batch);
  MetricsOperationStatus invalid_request(MetricsErrorCode code,
                                         std::string message,
                                         std::string stage) const;
  double simulated_latency_ms(const MetricExportBatch& batch) const;

  MetricsResolvedConfig config_;
  ExportBatchReport last_report_{};
  MetricsModuleSnapshot module_snapshot_{};
  std::uint64_t export_success_total_ = 0;
  std::uint64_t export_failure_total_ = 0;
  std::string last_rendered_text_;
};

}  // namespace dasall::infra::metrics