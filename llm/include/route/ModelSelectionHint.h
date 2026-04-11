#pragma once

#include <cstdint>
#include <string>

namespace dasall::llm {

struct ModelSelectionHint {
  std::string stage;
  std::string task_type;
  std::string complexity_tier;
  std::string latency_sla_tier;
  std::string budget_tier;
  bool requires_tools = false;
  bool requires_reasoning = false;
  bool prefers_visible_reasoning = false;
  std::uint32_t estimated_input_tokens = 0;
  std::uint32_t target_output_tokens = 0;
  std::uint32_t previous_route_failures = 0;
};

}  // namespace dasall::llm
