#pragma once

namespace dasall::infra {

struct ProbeResult;

class IHealthProbe {
 public:
  virtual ~IHealthProbe() = default;

  [[nodiscard]] virtual ProbeResult probe() = 0;
};

}  // namespace dasall::infra