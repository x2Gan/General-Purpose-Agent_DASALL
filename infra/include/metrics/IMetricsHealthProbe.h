#pragma once

#include "metrics/MetricsSnapshots.h"

namespace dasall::infra::metrics {

class IMetricsHealthProbe {
 public:
  virtual ~IMetricsHealthProbe() = default;

  [[nodiscard]] virtual MetricsModuleSnapshot snapshot() const = 0;
};

}  // namespace dasall::infra::metrics