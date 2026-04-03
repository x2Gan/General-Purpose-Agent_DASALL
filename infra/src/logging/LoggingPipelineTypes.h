#pragma once

#include <string_view>

#include "logging/LogTypes.h"

namespace dasall::infra::logging {

enum class SinkRoute {
  BasicFile,
  Audit,
};

[[nodiscard]] inline constexpr std::string_view sink_route_name(SinkRoute route) {
  switch (route) {
    case SinkRoute::BasicFile:
      return "basic_file";
    case SinkRoute::Audit:
      return "audit";
  }

  return "unknown";
}

struct RoutedLogRecord {
  SinkRoute route = SinkRoute::BasicFile;
  LogEvent event;
};

}  // namespace dasall::infra::logging