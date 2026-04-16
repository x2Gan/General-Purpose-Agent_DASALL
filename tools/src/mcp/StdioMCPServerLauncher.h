#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bridge/PluginExtensionBridge.h"
#include "mcp/MCPAdapter.h"
#include "mcp/StdioMCPTransport.h"

namespace dasall::tools::mcp {

struct StdioLaunchBindingTemplate {
  std::string internal_tool_name;
  std::string remote_tool_name;
  std::optional<std::string> remote_capability_id;
  std::optional<std::string> input_schema_ref;
};

struct StdioMCPLaunchSample {
  std::string launch_spec_ref;
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  std::string working_dir;
  std::string protocol_version;
  std::vector<std::string> declared_capabilities;
  std::vector<StdioLaunchBindingTemplate> tool_bindings;
  std::string healthcheck_mode;
};

using StdioLaunchSampleResolver =
    std::function<std::optional<StdioMCPLaunchSample>(std::string_view launch_spec_ref)>;
using StdioLaunchChannelBuilder = std::function<std::unique_ptr<IStdioTransportChannel>(
    const MCPServerSpec& spec,
    const StdioMCPLaunchSample& sample)>;

struct StdioMCPServerLauncherDependencies {
  StdioLaunchSampleResolver sample_resolver;
  StdioLaunchChannelBuilder channel_builder;
};

class StdioMCPServerLauncher {
 public:
  StdioMCPServerLauncher();
  explicit StdioMCPServerLauncher(StdioMCPServerLauncherDependencies dependencies);

  [[nodiscard]] std::optional<MCPServerSpec> build_server_spec(
      const bridge::MCPServerLaunchSpec& launch_spec) const;
  [[nodiscard]] std::vector<MCPToolBinding> build_bindings(
      const bridge::MCPServerLaunchSpec& launch_spec) const;
  [[nodiscard]] MCPTransportFactory build_transport_factory() const;

 private:
  [[nodiscard]] std::optional<StdioMCPLaunchSample> resolve_sample(
      std::string_view launch_spec_ref) const;

  StdioMCPServerLauncherDependencies dependencies_;
};

}  // namespace dasall::tools::mcp