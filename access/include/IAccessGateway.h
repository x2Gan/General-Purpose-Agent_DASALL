#pragma once

#include "AccessTypes.h"

namespace dasall::access {

class IAccessGateway {
 public:
  virtual ~IAccessGateway() = default;

  virtual bool init() = 0;
  virtual RuntimeDispatchResult submit(const InboundPacket& packet) = 0;
  virtual bool publish_result(const PublishEnvelope& envelope) = 0;
};

}  // namespace dasall::access