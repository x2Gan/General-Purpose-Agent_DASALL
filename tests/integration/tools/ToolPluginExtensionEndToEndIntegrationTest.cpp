#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bridge/PluginExtensionBridge.h"
#include "bridge/ToolPluginExtensionConsumer.h"
#include "bridge/ToolPluginLifecycleBridge.h"
#include "mcp/CapabilityCache.h"
#include "mcp/CapabilityDiscovery.h"
#include "plugin/IPluginManager.h"
#include "plugin/IToolPluginProvider.h"
#include "registry/ToolRegistry.h"
#include "skills/PluginSkillBundleImporter.h"
#include "skills/SkillRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kPluginId = "plugin.composite";
constexpr std::string_view kSourceKey = "plugin:plugin.composite";
constexpr std::string_view kToolName = "tool.plugin.echo";
constexpr std::string_view kServerId = "mcp.plugin.echo";
constexpr std::string_view kProviderHandleRef = "provider://plugin.composite/builtin.echo";
constexpr std::string_view kLaunchSpecRef = "launch://plugin.composite/mcp.echo.v1";

class ScriptedAdapter final : public dasall::tools::mcp::IMCPAdapter {
 public:
  struct Behavior {
    dasall::tools::mcp::MCPServerSession session;
    dasall::tools::CapabilitySnapshot snapshot;
  };

  std::map<std::string, Behavior> behaviors_by_server;

  [[nodiscard]] dasall::tools::mcp::MCPServerSession ensure_session(
      const dasall::tools::mcp::MCPServerSpec& spec) override {
    const auto it = behaviors_by_server.find(spec.server_id);
    if (it == behaviors_by_server.end()) {
      return {};
    }

    return it->second.session;
  }

  [[nodiscard]] dasall::tools::CapabilitySnapshot list_capabilities(
      const dasall::tools::mcp::MCPServerSession& session) override {
    const auto it = behaviors_by_server.find(session.server_id);
    if (it == behaviors_by_server.end()) {
      return dasall::tools::CapabilitySnapshot{
          .server_id = session.server_id,
          .entries = {},
          .freshness = dasall::tools::CapabilityFreshness::expired,
          .last_refresh_at_ms = std::nullopt,
          .last_error = std::string("mcp.discovery.unconfigured_server"),
          .trust_marker = std::nullopt,
      };
    }

    return it->second.snapshot;
  }

  [[nodiscard]] dasall::contracts::ToolResult invoke(
      const dasall::tools::mcp::MCPServerSession& session,
      const dasall::tools::mcp::MCPToolBinding& binding,
      const dasall::contracts::ToolIR& tool_ir) override {
    static_cast<void>(session);
    static_cast<void>(binding);
    static_cast<void>(tool_ir);
    return {};
  }
};

class FakePluginManager final : public dasall::infra::plugin::IPluginManager {
 public:
  [[nodiscard]] dasall::infra::plugin::PluginCatalog discover(
      std::string_view profile_id) const override {
    static_cast<void>(profile_id);
    return {};
  }

  [[nodiscard]] dasall::infra::plugin::PluginValidationResult validate(
      const dasall::infra::plugin::PluginValidationRequest& request) const override {
    return dasall::infra::plugin::PluginValidationResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        request.plugin_id,
        "validation is not used by ToolPluginExtensionEndToEndIntegrationTest",
        "plugin.validate",
        "FakePluginManager",
        "plugin.validation.unused");
  }

  dasall::infra::plugin::PluginLoadResult load(
      std::string_view plugin_id,
      const dasall::infra::plugin::PluginLoadOptions& load_options) override {
    static_cast<void>(load_options);
    return dasall::infra::plugin::PluginLoadResult::success(
        std::string(plugin_id),
        dasall::infra::plugin::PluginOperationPhase::Load,
        std::string("handle://") + std::string(plugin_id),
        std::string("audit://") + std::string(plugin_id) + "/load");
  }

  dasall::infra::plugin::PluginUnloadResult unload(std::string_view plugin_id) override {
    return dasall::infra::plugin::PluginUnloadResult::success(
        std::string(plugin_id),
        std::string("audit://") + std::string(plugin_id) + "/unload");
  }

  [[nodiscard]] dasall::infra::plugin::ActivePluginSet list_active() const override {
    return active_set_;
  }

  void set_active_plugins(std::vector<dasall::infra::plugin::PluginDescriptor> active_plugins,
                          bool safe_mode_active) {
    active_set_ = dasall::infra::plugin::ActivePluginSet{
        .active_plugins = std::move(active_plugins),
        .safe_mode_active = safe_mode_active,
        .max_active = 16,
    };
  }

 private:
  dasall::infra::plugin::ActivePluginSet active_set_;
};

[[nodiscard]] std::filesystem::path project_root() {
  auto root = std::filesystem::path(__FILE__);
  for (int level = 0; level < 4; ++level) {
    root = root.parent_path();
  }

  return root;
}

[[nodiscard]] dasall::infra::plugin::PluginDescriptor make_active_descriptor() {
  return dasall::infra::plugin::PluginDescriptor::normalize(
      dasall::infra::plugin::PluginDescriptor{
          .plugin_id = std::string(kPluginId),
          .version = std::string("1.0.0"),
          .abi = std::string("linux.gcc13"),
          .trust_level = dasall::infra::plugin::PluginTrustLevel::Internal,
          .status = dasall::infra::plugin::PluginStatus::Active,
          .source = std::string("./plugins/") + std::string(kPluginId),
      });
}

[[nodiscard]] dasall::tools::plugin::ToolPluginProviderRef make_provider_ref(
    const std::string& export_key,
    const std::string& source_revision) {
  return dasall::tools::plugin::ToolPluginProviderRef{
      .plugin_id = std::string(kPluginId),
      .export_key = export_key,
      .source_revision = source_revision,
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_plugin_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string(kToolName),
      .display_name = std::string("Plugin Echo"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/tool.plugin.echo/input/v1"),
      .output_schema_ref = std::string("schema://tools/tool.plugin.echo/output/v1"),
      .required_scopes = std::vector<std::string>{"trusted"},
      .tags = std::vector<std::string>{"plugin", "mcp"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::plugin::ToolPluginExtensionCatalog make_catalog() {
  using dasall::tools::plugin::BuiltinToolProviderExport;
  using dasall::tools::plugin::MCPServerStdioExport;
  using dasall::tools::plugin::SkillBundleExport;
  using dasall::tools::plugin::ToolPluginExtensionCatalog;
  using dasall::tools::plugin::ToolPluginPayloadKind;

  return ToolPluginExtensionCatalog{
      .payload_kinds = {
          ToolPluginPayloadKind::builtin_tool_provider,
          ToolPluginPayloadKind::mcp_server_stdio,
          ToolPluginPayloadKind::skill_bundle,
      },
      .builtin_tool_providers = {
          BuiltinToolProviderExport{
              .provider_ref = make_provider_ref("builtin.echo", "rev-a"),
              .provider_handle_ref = std::string(kProviderHandleRef),
          },
      },
      .mcp_stdio_servers = {
          MCPServerStdioExport{
              .provider_ref = make_provider_ref("mcp.echo", "rev-a"),
              .server_id = std::string(kServerId),
              .launch_spec_ref = std::string(kLaunchSpecRef),
              .trust_level = std::string("trusted-local"),
          },
      },
      .skill_bundles = {
          SkillBundleExport{
              .provider_ref = make_provider_ref("skills.github.runtime", "rev-a"),
              .bundle_id = std::string("bundle.github-runtime"),
              .asset_root_ref = std::string("skills/external_dialects/github"),
              .dialect_ref = std::string("github.skills"),
          },
      },
  };
}

[[nodiscard]] dasall::tools::CapabilitySnapshot make_success_snapshot() {
  return dasall::tools::CapabilitySnapshot{
      .server_id = std::string(kServerId),
      .entries = {
          dasall::tools::CapabilityEntry{
              .capability_id = std::string("cap.plugin.echo"),
              .capability_version = std::string("1.0.0"),
              .tool_names = {std::string(kToolName)},
          },
      },
      .freshness = dasall::tools::CapabilityFreshness::fresh,
      .last_refresh_at_ms = 1000,
      .last_error = std::nullopt,
      .trust_marker = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::mcp::StdioMCPLaunchSample make_launch_sample() {
  return dasall::tools::mcp::StdioMCPLaunchSample{
      .launch_spec_ref = std::string(kLaunchSpecRef),
      .command = std::string("tests/mocks/bin/mcp-loopback-server"),
      .args = {"--scenario", "plugin-echo"},
      .env = {{"MCP_PROTOCOL_VERSION", "2025-03-26"}},
      .working_dir = std::string("${workspaceFolder}"),
      .protocol_version = std::string("2025-03-26"),
      .declared_capabilities = {"tools"},
      .tool_bindings = {
          dasall::tools::mcp::StdioLaunchBindingTemplate{
              .internal_tool_name = std::string(kToolName),
              .remote_tool_name = std::string("remote.plugin.echo"),
              .remote_capability_id = std::string("cap.plugin.echo"),
              .input_schema_ref = std::string("schema://tools/tool.plugin.echo/input/v1"),
          },
      },
      .healthcheck_mode = std::string("initialize_roundtrip"),
  };
}

[[nodiscard]] std::shared_ptr<dasall::tools::mcp::StdioMCPServerLauncher> make_launcher() {
  const auto sample = make_launch_sample();
  return std::make_shared<dasall::tools::mcp::StdioMCPServerLauncher>(
      dasall::tools::mcp::StdioMCPServerLauncherDependencies{
          .sample_resolver = [sample](std::string_view launch_spec_ref)
              -> std::optional<dasall::tools::mcp::StdioMCPLaunchSample> {
            if (launch_spec_ref != sample.launch_spec_ref) {
              return std::nullopt;
            }
            return sample;
          },
          .channel_builder = {},
      });
}

void test_lifecycle_bridge_distributes_builtin_mcp_and_skill_payloads() {
  std::int64_t now_ms = 1000;
  auto plugin_manager = std::make_shared<FakePluginManager>();
  auto bridge = std::make_shared<dasall::tools::bridge::PluginExtensionBridge>();
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{});
  auto cache = std::make_shared<dasall::tools::mcp::CapabilityCache>(
      dasall::tools::mcp::CapabilityCacheOptions{
          .expire_after_ms = 5000,
          .stale_read_allowed = true,
          .now_ms = [&now_ms]() { return now_ms; },
      });
  auto adapter = std::make_shared<ScriptedAdapter>();
  adapter->behaviors_by_server[std::string(kServerId)] = ScriptedAdapter::Behavior{
      .session = {
          .server_id = std::string(kServerId),
          .transport_kind = dasall::tools::mcp::MCPTransportKind::stdio,
          .session_ref = std::string("sess-plugin-echo"),
          .negotiated_protocol_version = std::string("2025-03-26"),
          .transport_connection_id = std::string("conn-plugin-echo"),
      },
      .snapshot = make_success_snapshot(),
  };
  auto discovery = std::make_shared<dasall::tools::mcp::CapabilityDiscovery>(
      dasall::tools::mcp::CapabilityDiscoveryDependencies{
          .capability_cache = cache,
          .adapter = adapter,
          .registry = registry,
          .launcher = make_launcher(),
          .now_ms = [&now_ms]() { return now_ms; },
          .refresh_interval_ms = 500,
          .failure_backoff_ms = 200,
      });
  auto skill_registry = std::make_shared<dasall::tools::skills::SkillRegistry>();
  dasall::tools::skills::PluginSkillBundleImporter skill_importer(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = true,
          .project_root = project_root(),
      });
  auto consumer = std::make_shared<dasall::tools::bridge::ToolPluginExtensionConsumer>(
      dasall::tools::bridge::ToolPluginExtensionConsumerDependencies{
          .registry = registry,
          .discovery = discovery,
          .skill_registry = skill_registry,
          .builtin_descriptor_resolver = [](const auto& provider_view)
              -> std::optional<std::vector<dasall::contracts::ToolDescriptor>> {
            if (provider_view.provider_handle_ref != kProviderHandleRef) {
              return std::nullopt;
            }

            return std::vector<dasall::contracts::ToolDescriptor>{make_plugin_descriptor()};
          },
          .skill_bundle_importer = [&skill_importer](const auto& skill_asset_ref) {
            return skill_importer.import_bundle(skill_asset_ref);
          },
      });

  const auto catalog = make_catalog();
  std::map<std::string, dasall::tools::plugin::ToolPluginExtensionCatalog> catalogs_by_plugin{
      {std::string(kPluginId), catalog},
  };
  std::map<std::string, dasall::tools::plugin::ToolPluginExtensionCatalog> catalogs_by_handle{
      {std::string("handle://") + std::string(kPluginId), catalog},
  };

  dasall::tools::bridge::ToolPluginLifecycleBridge lifecycle_bridge(
      plugin_manager,
      bridge,
      [&catalogs_by_plugin, &catalogs_by_handle](std::string_view plugin_id,
                                                 std::string_view handle_ref)
          -> std::optional<dasall::tools::plugin::ToolPluginExtensionCatalog> {
        if (!handle_ref.empty()) {
          const auto handle_it = catalogs_by_handle.find(std::string(handle_ref));
          if (handle_it != catalogs_by_handle.end()) {
            return handle_it->second;
          }
        }

        const auto plugin_it = catalogs_by_plugin.find(std::string(plugin_id));
        if (plugin_it == catalogs_by_plugin.end()) {
          return std::nullopt;
        }

        return plugin_it->second;
      },
      consumer);

  plugin_manager->set_active_plugins({make_active_descriptor()}, false);

  assert_equal(1,
               static_cast<int>(lifecycle_bridge.synchronize_active_plugins()),
               "lifecycle bridge should publish one active plugin into bridge snapshot and downstream tool domains");
  assert_true(bridge->snapshot()->builtin_providers_by_source.find(std::string(kSourceKey)) !=
                  bridge->snapshot()->builtin_providers_by_source.end(),
              "lifecycle bridge should still publish the builtin provider snapshot entry");
  assert_true(registry->resolve_descriptor(kToolName).has_value(),
              "consumer should publish plugin-delivered builtin descriptors into ToolRegistry");
  assert_true(discovery->resolve_server_spec(kServerId).has_value(),
              "consumer should publish plugin-delivered MCP launch specs into CapabilityDiscovery");
  assert_equal(2, static_cast<int>(skill_registry->list_assets().size()),
               "consumer should import and register plugin-delivered skill bundle assets");

  const auto refresh_summary = discovery->refresh_once();
  assert_equal(1, static_cast<int>(refresh_summary.refreshed_server_ids.size()),
               "refresh should publish bindings for the plugin-delivered MCP server");
  assert_equal(1, static_cast<int>(registry->list_mcp_bindings(kToolName).size()),
               "plugin-delivered MCP launch specs should publish one binding after refresh");
  assert_true(cache->snapshot(kServerId).has_value(),
              "successful refresh should populate the capability cache for the plugin-delivered MCP server");

  const auto unload_result = dasall::infra::plugin::PluginUnloadResult::success(
      std::string(kPluginId),
      std::string("audit://") + std::string(kPluginId) + "/unload");
  assert_true(lifecycle_bridge.on_plugin_unloaded(unload_result),
              "plugin unload should revoke bridge snapshot and all downstream tool domains");

  assert_true(bridge->snapshot()->builtin_providers_by_source.empty(),
              "plugin unload should clear the bridge-side builtin provider snapshot entry");
  assert_true(!registry->resolve_descriptor(kToolName).has_value(),
              "plugin unload should remove the plugin-delivered builtin descriptor");
  assert_equal(0, static_cast<int>(registry->list_mcp_bindings(kToolName).size()),
               "plugin unload should revoke the plugin-delivered MCP binding batch");
  assert_true(!discovery->resolve_server_spec(kServerId).has_value(),
              "plugin unload should revoke the plugin-delivered MCP server record");
  assert_true(!cache->snapshot(kServerId).has_value(),
              "plugin unload should invalidate the plugin-delivered capability cache entry");
  assert_true(skill_registry->list_assets().empty(),
              "plugin unload should revoke the plugin-delivered skill assets");
}

}  // namespace

int main() {
  try {
    test_lifecycle_bridge_distributes_builtin_mcp_and_skill_payloads();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}