#include "builtin/dataset/AgentDatasetTool.h"

#include <string>
#include <vector>

namespace {

[[nodiscard]] std::string resolve_tool_name(const dasall::contracts::ToolIR& tool_ir) {
  if (tool_ir.tool_name.has_value() && !tool_ir.tool_name->empty()) {
    return *tool_ir.tool_name;
  }

  return std::string(dasall::tools::builtin::dataset::kToolName);
}

}  // namespace

namespace dasall::tools::builtin::dataset {

bool matches(std::string_view tool_name) {
  return tool_name == kToolName;
}

contracts::ToolDescriptor build_descriptor() {
  return contracts::ToolDescriptor{
      .tool_name = std::string(kToolName),
      .display_name = std::string("Agent Dataset"),
      .category = contracts::ToolCategory::Information,
      .capability_tier = contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.dataset/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.dataset/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"builtin", "dataset"},
      .version = std::string("1.0.0"),
  };
}

services::DataQueryRequest build_query_request(
    const contracts::ToolIR& tool_ir,
    const services::ServiceCallContext& context,
    services::ServiceDataFreshness freshness) {
  return services::DataQueryRequest{
      .context = context,
      .dataset = resolve_tool_name(tool_ir),
      .filters_json = tool_ir.normalized_arguments.value_or(std::string("{}")),
      .projection = std::string("default"),
      .freshness = freshness,
  };
}

}  // namespace dasall::tools::builtin::dataset