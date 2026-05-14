#include "AccessIdGenerator.h"

#include <string>

namespace dasall::access {

std::string AccessIdGenerator::generate(std::string_view prefix,
                                        const RuntimeDispatchRequest& request,
                                        const std::size_t ordinal) const {
  return std::string(prefix) + ":" + request.packet.entry_type + ":" +
         request.packet.packet_id + ":" + std::to_string(ordinal);
}

}  // namespace dasall::access