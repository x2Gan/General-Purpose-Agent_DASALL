#pragma once

#include <string_view>

#include "logging/LogTypes.h"

namespace dasall::infra::logging {

class StructuredFormatter {
 public:
  static constexpr std::string_view kSchemaVersion =
      "dasall.logging.event.v1";

  [[nodiscard]] LogEvent format(const LogEvent& event) const;
};

}  // namespace dasall::infra::logging