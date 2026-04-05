#pragma once

#include "config/IConfigCenter.h"
#include "policy/IPolicyLoader.h"

namespace dasall::infra::policy {

class PolicyLoader final : public IPolicyLoader {
 public:
  explicit PolicyLoader(const config::IConfigCenter& config_center);

  [[nodiscard]] PolicyBundle load_from_sources() override;

 private:
  const config::IConfigCenter& config_center_;
};

}  // namespace dasall::infra::policy