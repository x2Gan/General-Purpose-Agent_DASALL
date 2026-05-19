#include "builtin/terminal/AgentTerminalTool.h"

#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string resolve_tool_name(const dasall::contracts::ToolIR& tool_ir) {
  if (tool_ir.tool_name.has_value() && !tool_ir.tool_name->empty()) {
    return *tool_ir.tool_name;
  }

  return std::string(dasall::tools::builtin::terminal::kToolName);
}

[[nodiscard]] dasall::services::CapabilityTargetRef build_target(std::string_view capability_id) {
  return dasall::services::CapabilityTargetRef{
      .capability_id = std::string(capability_id),
      .target_id = std::string("builtin:") + std::string(capability_id),
  };
}

}  // namespace

namespace dasall::tools::builtin::terminal {

bool matches(std::string_view tool_name) {
  return tool_name == kToolName;
}

contracts::ToolDescriptor build_descriptor() {
  return contracts::ToolDescriptor{
      .tool_name = std::string(kToolName),
      .display_name = std::string("Agent Terminal"),
      .category = contracts::ToolCategory::Action,
      .capability_tier = contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = true,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "terminal"},
      .version = std::string("1.0.0"),
  };
}

services::ExecutionCommandRequest build_action_request(
    const contracts::ToolIR& tool_ir,
    const services::ServiceCallContext& context) {
  const auto tool_name = resolve_tool_name(tool_ir);
  return services::ExecutionCommandRequest{
      .context = context,
      .target = build_target(tool_name),
      .action = tool_name,
      .arguments_json = tool_ir.normalized_arguments.value_or(std::string("{}")),
      .idempotency_key = tool_ir.idempotency_key,
  };
}

}  // namespace dasall::tools::builtin::terminal