#pragma once

#include <string>

namespace dasall::cognition {

struct CognitionConfig {
  std::string default_tool_name = "agent.dataset";
  float ask_clarification_threshold = 0.45F;
};

}  // namespace dasall::cognition