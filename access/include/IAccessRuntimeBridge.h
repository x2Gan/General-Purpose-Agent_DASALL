#pragma once

#include <string_view>

#include "AccessTypes.h"

namespace dasall::access {

class IAccessRuntimeBridge {
 public:
  virtual ~IAccessRuntimeBridge() = default;

  virtual RuntimeDispatchResult dispatch(const RuntimeDispatchRequest& request) = 0;
  virtual bool cancel(std::string_view request_id, std::string_view actor_ref) = 0;
};

}  // namespace dasall::access