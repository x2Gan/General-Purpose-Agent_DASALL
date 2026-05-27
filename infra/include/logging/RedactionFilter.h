#pragma once

#include "logging/LogTypes.h"

namespace dasall::infra::logging {

class RedactionFilter {
 public:
  [[nodiscard]] LogEvent apply(const LogEvent& event) const;
};

}  // namespace dasall::infra::logging