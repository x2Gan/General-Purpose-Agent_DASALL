#pragma once

#include <optional>
#include <string>
#include <vector>

#include "context/ContextPacket.h"
#include "error/ResultCode.h"

namespace dasall::contracts {

struct ContextAssembleResult {
  std::optional<ResultCode> result_code;
  ContextPacket context_packet;
  std::vector<std::string> dropped_sections;
  std::vector<std::string> compression_notes;
  std::vector<std::string> warnings;
  bool degraded = false;
};

}  // namespace dasall::contracts