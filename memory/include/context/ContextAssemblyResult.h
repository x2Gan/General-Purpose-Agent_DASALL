#pragma once

#include <optional>
#include <string>
#include <vector>

#include "context/ContextPacket.h"
#include "error/ResultCode.h"

namespace dasall::memory {

struct ContextAssemblyResult {
  std::optional<contracts::ResultCode> result_code;
  contracts::ContextPacket context_packet;
  std::vector<std::string> dropped_sections;
  std::vector<std::string> compression_notes;
  std::vector<std::string> warnings;
  bool degraded = false;
};

}  // namespace dasall::memory