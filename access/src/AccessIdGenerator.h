#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "AccessTypes.h"

namespace dasall::access {

class AccessIdGenerator final {
 public:
  [[nodiscard]] std::string generate(std::string_view prefix,
                                     const RuntimeDispatchRequest& request,
                                     std::size_t ordinal) const;
};

}  // namespace dasall::access