#include "metrics/MetricsExporterAdapter.h"

#include <string>
#include <utility>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricsExporterAdapterSourceRef = "MetricsExporterAdapter";

[[nodiscard]] MetricsOperationStatus make_exporter_failure(MetricsErrorCode code,
                                                           std::string message,
                                                           std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kMetricsExporterAdapterSourceRef) + ":" +
          std::string(metrics_error_code_name(code)));
}

}  // namespace

MetricsExporterAdapter::MetricsExporterAdapter()
    : MetricsExporterAdapter(MetricsConfigPolicy{}.default_config()) {}

MetricsExporterAdapter::MetricsExporterAdapter(MetricsResolvedConfig config)
    : config_(config.is_valid() ? std::move(config) : MetricsConfigPolicy{}.default_config()) {
  module_snapshot_.exporter_state = config_.exporter_type;
}

MetricsOperationStatus MetricsExporterAdapter::export_batch(const MetricExportBatch& batch) {
  if (!batch.is_valid()) {
    return invalid_request(MetricsErrorCode::ConfigInvalid,
                           "metrics exporter adapter requires a valid export batch",
                           "metrics.exporter.export_batch");
  }

  const std::string effective_exporter_type =
      batch.exporter_type.empty() ? config_.exporter_type : batch.exporter_type;
  module_snapshot_.exporter_state = effective_exporter_type;

  if (effective_exporter_type == "noop") {
    return export_noop(batch);
  }

  if (effective_exporter_type == "prom_text") {
    return export_prom_text(batch);
  }

  return export_unsupported(batch);
}

MetricsOperationStatus MetricsExporterAdapter::force_flush(const MetricsCallDeadline& timeout) {
  if (!timeout.is_valid()) {
    return invalid_request(MetricsErrorCode::ConfigInvalid,
                           "metrics exporter adapter requires a positive flush deadline",
                           "metrics.exporter.force_flush");
  }

  return MetricsOperationStatus::success("metrics-exporter://flushed");
}

MetricsOperationStatus MetricsExporterAdapter::shutdown(const MetricsCallDeadline& timeout) {
  if (!timeout.is_valid()) {
    return invalid_request(MetricsErrorCode::ConfigInvalid,
                           "metrics exporter adapter requires a positive shutdown deadline",
                           "metrics.exporter.shutdown");
  }

  module_snapshot_.exporter_state = "stopped";
  return MetricsOperationStatus::success("metrics-exporter://stopped");
}

MetricsOperationStatus MetricsExporterAdapter::fallback_to_noop(std::string reason) {
  config_.exporter_type = "noop";
  module_snapshot_.exporter_state = "noop";
  module_snapshot_.degraded = true;
  last_rendered_text_.clear();
  return MetricsOperationStatus::success("metrics-exporter://fallback-noop:" + std::move(reason));
}

const ExportBatchReport& MetricsExporterAdapter::last_report() const {
  return last_report_;
}

const MetricsModuleSnapshot& MetricsExporterAdapter::module_snapshot() const {
  return module_snapshot_;
}

std::uint64_t MetricsExporterAdapter::export_success_total() const {
  return export_success_total_;
}

std::uint64_t MetricsExporterAdapter::export_failure_total() const {
  return export_failure_total_;
}

const std::string& MetricsExporterAdapter::last_rendered_text() const {
  return last_rendered_text_;
}

MetricsOperationStatus MetricsExporterAdapter::export_noop(const MetricExportBatch& batch) {
  export_success_total_ += batch.sample_count;
  last_report_ = ExportBatchReport{
      .success_count = static_cast<std::uint64_t>(batch.sample_count),
      .fail_count = 0U,
      .latency_ms = 0.0,
      .dropped_count = 0U,
  };
  module_snapshot_.degraded = false;
  module_snapshot_.exporter_state = "noop";
  last_rendered_text_.clear();
  return MetricsOperationStatus::success("metrics-exporter://noop");
}

MetricsOperationStatus MetricsExporterAdapter::export_prom_text(const MetricExportBatch& batch) {
  const auto latency_ms = simulated_latency_ms(batch);
  if (latency_ms > static_cast<double>(config_.exporter_timeout_ms)) {
    export_failure_total_ += batch.sample_count;
    last_report_ = ExportBatchReport{
        .success_count = 0U,
        .fail_count = static_cast<std::uint64_t>(batch.sample_count),
        .latency_ms = latency_ms,
        .dropped_count = 0U,
    };
    (void)fallback_to_noop("timeout");
    return invalid_request(MetricsErrorCode::ExportTimeout,
                           "metrics exporter adapter timed out before prom_text export completed",
                           "metrics.exporter.export_batch");
  }

  export_success_total_ += batch.sample_count;
  last_report_ = ExportBatchReport{
      .success_count = static_cast<std::uint64_t>(batch.sample_count),
      .fail_count = 0U,
      .latency_ms = latency_ms,
      .dropped_count = 0U,
  };
  module_snapshot_.degraded = false;
  module_snapshot_.exporter_state = "prom_text";
  last_rendered_text_ = "# TYPE metrics_export_samples_total counter\n";
  last_rendered_text_ += "metrics_export_samples_total{batch_id=\"" + batch.batch_id +
                         "\"} " + std::to_string(batch.sample_count) + "\n";
  return MetricsOperationStatus::success("metrics-exporter://prom-text");
}

MetricsOperationStatus MetricsExporterAdapter::export_unsupported(const MetricExportBatch& batch) {
  export_failure_total_ += batch.sample_count;
  last_report_ = ExportBatchReport{
      .success_count = 0U,
      .fail_count = static_cast<std::uint64_t>(batch.sample_count),
      .latency_ms = 0.0,
      .dropped_count = 0U,
  };
  (void)fallback_to_noop("unsupported-exporter");
  return invalid_request(MetricsErrorCode::ExportFailure,
                         "metrics exporter adapter only supports noop/prom_text during the first export skeleton round",
                         "metrics.exporter.export_batch");
}

MetricsOperationStatus MetricsExporterAdapter::invalid_request(MetricsErrorCode code,
                                                               std::string message,
                                                               std::string stage) const {
  return make_exporter_failure(code, std::move(message), std::move(stage));
}

double MetricsExporterAdapter::simulated_latency_ms(const MetricExportBatch& batch) const {
  return static_cast<double>(batch.sample_count);
}

}  // namespace dasall::infra::metrics