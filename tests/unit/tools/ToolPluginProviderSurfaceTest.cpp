#include <exception>
#include <iostream>

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "plugin/IToolPluginProvider.h"

namespace {

using dasall::tools::plugin::BuiltinToolProviderExport;
using dasall::tools::plugin::IToolPluginProvider;
using dasall::tools::plugin::MCPServerStdioExport;
using dasall::tools::plugin::SkillBundleExport;
using dasall::tools::plugin::ToolPluginExtensionCatalog;
using dasall::tools::plugin::ToolPluginPayloadKind;
using dasall::tools::plugin::ToolPluginProviderRef;

static_assert(std::is_same_v<decltype(&IToolPluginProvider::describe_extensions),
                             ToolPluginExtensionCatalog (IToolPluginProvider::*)()
                                 const>);
static_assert(std::is_abstract_v<IToolPluginProvider>);

void plugin_extension_catalog_only_expresses_supported_payload_kinds() {
  const ToolPluginProviderRef builtin_ref{
    .plugin_id = std::string("plugin.shell"),
    .export_key = std::string("builtin.shell"),
    .source_revision = std::string("rev-7"),
  };

  const ToolPluginProviderRef mcp_ref{
    .plugin_id = std::string("plugin.shell"),
    .export_key = std::string("mcp.shell.stdio"),
    .source_revision = std::string("rev-7"),
  };

  const ToolPluginProviderRef skill_ref{
    .plugin_id = std::string("plugin.shell"),
    .export_key = std::string("skills.shell"),
    .source_revision = std::string("rev-7"),
  };

  const ToolPluginExtensionCatalog catalog{
    .payload_kinds = {
      ToolPluginPayloadKind::builtin_tool_provider,
      ToolPluginPayloadKind::mcp_server_stdio,
      ToolPluginPayloadKind::skill_bundle,
    },
    .builtin_tool_providers = {
      BuiltinToolProviderExport{
        .provider_ref = builtin_ref,
        .provider_handle_ref = std::string("handle://builtin-shell"),
      },
    },
    .mcp_stdio_servers = {
      MCPServerStdioExport{
        .provider_ref = mcp_ref,
        .server_id = std::string("mcp://shell"),
        .launch_spec_ref = std::string("launch://shell-stdio"),
        .trust_level = std::string("trusted-local"),
      },
    },
    .skill_bundles = {
      SkillBundleExport{
        .provider_ref = skill_ref,
        .bundle_id = std::string("shell-bundle"),
        .asset_root_ref = std::string("asset://skills/shell"),
        .dialect_ref = std::string("dasall.skill.v1"),
      },
    },
  };

  static_cast<void>(catalog);
}

}  // namespace

int main() {
  try {
    plugin_extension_catalog_only_expresses_supported_payload_kinds();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}