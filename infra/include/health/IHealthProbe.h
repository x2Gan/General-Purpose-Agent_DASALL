#pragma once

#include "health/ProbeTypes.h"

namespace dasall::infra {

class IHealthProbe {
 public:
  virtual ~IHealthProbe() = default;

  [[nodiscard]] virtual ProbeResult probe() = 0;
};

}  // namespace dasall::infra