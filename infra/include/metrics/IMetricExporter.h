#pragma once

#include <cstddef>
#include <string>

#include "metrics/IMetricsProvider.h"

namespace dasall::infra::metrics {

struct MetricExportBatch {
  std::string batch_id;
  std::size_t sample_count = 0;
  std::string exporter_type = "noop";

  [[nodiscard]] bool is_valid() const {
    return !batch_id.empty() && sample_count > 0 && !exporter_type.empty();
  }
};

class IMetricExporter {
 public:
  virtual ~IMetricExporter() = default;

  virtual MetricsOperationStatus export_batch(const MetricExportBatch& batch) = 0;
  virtual MetricsOperationStatus force_flush(const MetricsCallDeadline& timeout) = 0;
  virtual MetricsOperationStatus shutdown(const MetricsCallDeadline& timeout) = 0;
};

}  // namespace dasall::infra::metrics