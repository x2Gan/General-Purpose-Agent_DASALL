#pragma once

#include <string_view>

#include "AccessTypes.h"

namespace dasall::access {

class IProtocolAdapter {
 public:
  virtual ~IProtocolAdapter() = default;

  virtual bool can_handle(std::string_view entry_type,
                          std::string_view protocol_kind) const = 0;
  virtual InboundPacket decode() = 0;
  virtual bool encode(const PublishEnvelope& envelope) = 0;
};

}  // namespace dasall::access