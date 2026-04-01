#pragma once

#include "PolicyTypes.h"

namespace dasall::infra::policy {

class IPolicyLoader {
 public:
  virtual ~IPolicyLoader() = default;

  [[nodiscard]] virtual PolicyBundle load_from_sources() = 0;
};

}  // namespace dasall::infra::policy