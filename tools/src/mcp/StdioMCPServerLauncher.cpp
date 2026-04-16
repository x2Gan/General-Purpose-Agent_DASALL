#include "mcp/StdioMCPServerLauncher.h"

#include <utility>

namespace dasall::tools::mcp {

StdioMCPServerLauncher::StdioMCPServerLauncher() = default;

StdioMCPServerLauncher::StdioMCPServerLauncher(
    StdioMCPServerLauncherDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

std::optional<MCPServerSpec> StdioMCPServerLauncher::build_server_spec(
    const bridge::MCPServerLaunchSpec& launch_spec) const {
  const auto sample = resolve_sample(launch_spec.launch_spec_ref);
  if (!sample.has_value() || launch_spec.server_id.empty() ||
      launch_spec.trust_level.empty() || sample->command.empty() ||
      sample->protocol_version.empty()) {
    return std::nullopt;
  }

  return MCPServerSpec{
      .server_id = launch_spec.server_id,
      .transport_kind = MCPTransportKind::stdio,
      .endpoint_ref = launch_spec.launch_spec_ref,
      .declared_capabilities = sample->declared_capabilities.empty()
                                 ? std::vector<std::string>{"tools"}
                                 : sample->declared_capabilities,
      .trust_level = launch_spec.trust_level,
      .healthcheck_ref = sample->healthcheck_mode.empty()
                             ? std::nullopt
                             : std::optional<std::string>(sample->healthcheck_mode),
  };
}

std::vector<MCPToolBinding> StdioMCPServerLauncher::build_bindings(
    const bridge::MCPServerLaunchSpec& launch_spec) const {
  const auto sample = resolve_sample(launch_spec.launch_spec_ref);
  if (!sample.has_value()) {
    return {};
  }

  std::vector<MCPToolBinding> bindings;
  bindings.reserve(sample->tool_bindings.size());

  for (const auto& binding : sample->tool_bindings) {
    if (binding.internal_tool_name.empty() || binding.remote_tool_name.empty()) {
      continue;
    }

    bindings.push_back(MCPToolBinding{
        .internal_tool_name = binding.internal_tool_name,
        .remote_tool_name = binding.remote_tool_name,
        .server_id = launch_spec.server_id,
        .remote_capability_id = binding.remote_capability_id,
        .input_schema_ref = binding.input_schema_ref,
    });
  }

  return bindings;
}

MCPTransportFactory StdioMCPServerLauncher::build_transport_factory() const {
  const auto sample_resolver = dependencies_.sample_resolver;
  const auto channel_builder = dependencies_.channel_builder;

  return [sample_resolver, channel_builder](MCPTransportKind kind) -> std::shared_ptr<IMCPTransport> {
    if (kind != MCPTransportKind::stdio || !sample_resolver || !channel_builder) {
      return nullptr;
    }

    return std::make_shared<StdioMCPTransport>(
        [sample_resolver, channel_builder](const MCPServerSpec& spec) {
          const auto sample = sample_resolver(spec.endpoint_ref);
          if (!sample.has_value()) {
            return std::unique_ptr<IStdioTransportChannel>{};
          }
          return channel_builder(spec, *sample);
        });
  };
}

std::optional<StdioMCPLaunchSample> StdioMCPServerLauncher::resolve_sample(
    std::string_view launch_spec_ref) const {
  if (!dependencies_.sample_resolver || launch_spec_ref.empty()) {
    return std::nullopt;
  }

  return dependencies_.sample_resolver(launch_spec_ref);
}

}  // namespace dasall::tools::mcp