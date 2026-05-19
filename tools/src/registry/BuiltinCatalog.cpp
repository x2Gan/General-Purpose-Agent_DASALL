#include "BuiltinCatalog.h"

namespace dasall::tools::registry {

std::vector<contracts::ToolDescriptor> build_builtin_catalog() {
  return {
      contracts::ToolDescriptor{
          .tool_name = std::string("agent.terminal"),
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
      },
          contracts::ToolDescriptor{
            .tool_name = std::string("agent.dataset"),
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
          },
  };
}

}  // namespace dasall::tools::registry