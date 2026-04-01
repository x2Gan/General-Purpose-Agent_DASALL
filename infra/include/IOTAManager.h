#pragma once

#include "ota/OTATypes.h"

namespace dasall::infra::ota {

class IOTAManager {
 public:
  virtual ~IOTAManager() = default;

  [[nodiscard]] virtual PrecheckReport precheck(const UpgradePlan& plan) const = 0;
  virtual UpgradeOutcome apply(const UpgradePlan& plan) = 0;
  virtual UpgradeOutcome rollback(const RollbackToken& token) = 0;
  [[nodiscard]] virtual OTAStatusSnapshot query_status() const = 0;
};

}  // namespace dasall::infra::ota