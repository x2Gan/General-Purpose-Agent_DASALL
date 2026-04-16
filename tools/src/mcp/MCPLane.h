#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "ToolManager.h"
#include "registry/ToolRegistry.h"

namespace dasall::tools::mcp {

using MCPServerSpecResolver = std::function<std::optional<MCPServerSpec>(std::string_view server_id)>;

struct MCPLaneDependencies {
  std::shared_ptr<registry::ToolRegistry> registry;
  std::shared_ptr<IMCPAdapter> adapter;
  MCPServerSpecResolver server_spec_resolver;
};

class MCPLane {
 public:
  MCPLane();
  explicit MCPLane(MCPLaneDependencies dependencies);

  [[nodiscard]] contracts::ToolResult execute(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context,
      std::optional<std::string> preferred_server_id = std::nullopt) const;
  [[nodiscard]] std::optional<MCPToolBinding> resolve_binding(
      std::string_view tool_name,
      std::optional<std::string_view> preferred_server_id = std::nullopt) const;
  [[nodiscard]] MCPServerSession ensure_session_ready(const MCPToolBinding& binding) const;

 private:
  [[nodiscard]] static MCPLaneDependencies default_dependencies();
  [[nodiscard]] contracts::ToolResult build_failure_result(
      const contracts::ToolIR& tool_ir,
      const ToolExecutionContext& execution_context,
      std::string reason_code,
      std::string stage) const;

  MCPLaneDependencies dependencies_;
};

}  // namespace dasall::tools::mcp