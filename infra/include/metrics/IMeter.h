#pragma once

#include <optional>
#include <string>

#include "metrics/IMetricsProvider.h"

namespace dasall::infra::metrics {

struct MetricIdentity;
struct MetricSample;

struct InstrumentHandle {
  std::string instrument_key;

  [[nodiscard]] bool is_valid() const {
    return !instrument_key.empty();
  }
};

class IMeter {
 public:
  virtual ~IMeter() = default;

  [[nodiscard]] virtual std::optional<InstrumentHandle> create_counter(
      const MetricIdentity& identity) = 0;
  [[nodiscard]] virtual std::optional<InstrumentHandle> create_gauge(
      const MetricIdentity& identity) = 0;
  [[nodiscard]] virtual std::optional<InstrumentHandle> create_histogram(
      const MetricIdentity& identity) = 0;
  virtual MetricsOperationStatus record(const MetricSample& sample) = 0;
};

}  // namespace dasall::infra::metrics