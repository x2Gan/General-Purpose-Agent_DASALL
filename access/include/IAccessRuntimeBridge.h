#pragma once

#include "AccessTypes.h"

namespace dasall::access {

class IAccessRuntimeBridge {
 public:
  virtual ~IAccessRuntimeBridge() = default;

  virtual RuntimeDispatchResult dispatch(const RuntimeDispatchRequest& request) = 0;
};

}  // namespace dasall::access