#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::tools::plugin {

enum class ToolPluginPayloadKind {
	builtin_tool_provider,
	mcp_server_stdio,
	skill_bundle,
};

struct ToolPluginProviderRef {
	std::string plugin_id;
	std::string export_key;
	std::string source_revision;
};

struct BuiltinToolProviderExport {
	ToolPluginProviderRef provider_ref;
	std::string provider_handle_ref;
};

struct MCPServerStdioExport {
	ToolPluginProviderRef provider_ref;
	std::string server_id;
	std::string launch_spec_ref;
	std::string trust_level;
};

struct SkillBundleExport {
	ToolPluginProviderRef provider_ref;
	std::string bundle_id;
	std::string asset_root_ref;
	std::optional<std::string> dialect_ref;
};

struct ToolPluginExtensionCatalog {
	std::vector<ToolPluginPayloadKind> payload_kinds;
	std::vector<BuiltinToolProviderExport> builtin_tool_providers;
	std::vector<MCPServerStdioExport> mcp_stdio_servers;
	std::vector<SkillBundleExport> skill_bundles;

	[[nodiscard]] bool empty() const {
		return builtin_tool_providers.empty() && mcp_stdio_servers.empty() &&
					 skill_bundles.empty();
	}
};

class IToolPluginProvider {
 public:
	virtual ~IToolPluginProvider() = default;

	[[nodiscard]] virtual ToolPluginExtensionCatalog describe_extensions() const = 0;
};

}  // namespace dasall::tools::plugin