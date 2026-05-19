#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bridge/PluginExtensionBridge.h"
#include "bridge/ToolPluginLifecycleBridge.h"
#include "plugin/IPluginManager.h"
#include "plugin/IToolPluginProvider.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::infra::plugin::PluginDescriptor make_descriptor(
    const std::string& plugin_id,
    dasall::infra::plugin::PluginStatus status) {
  return dasall::infra::plugin::PluginDescriptor::normalize(
      dasall::infra::plugin::PluginDescriptor{
          .plugin_id = plugin_id,
          .version = std::string("1.0.0"),
          .abi = std::string("linux.gcc13"),
          .trust_level = dasall::infra::plugin::PluginTrustLevel::Internal,
          .status = status,
          .source = std::string("./plugins/") + plugin_id,
      });
}

[[nodiscard]] dasall::tools::plugin::ToolPluginProviderRef make_provider_ref(
    const std::string& plugin_id,
    const std::string& export_key,
    const std::string& source_revision) {
  return dasall::tools::plugin::ToolPluginProviderRef{
      .plugin_id = plugin_id,
      .export_key = export_key,
      .source_revision = source_revision,
  };
}

[[nodiscard]] dasall::tools::plugin::ToolPluginExtensionCatalog make_catalog(
    const std::string& plugin_id,
    const std::string& source_revision,
    const std::string& suffix) {
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
              .provider_ref = make_provider_ref(
                  plugin_id,
                  std::string("builtin.") + suffix,
                  source_revision),
              .provider_handle_ref = std::string("provider://") + plugin_id + "/" + suffix,
          },
      },
      .mcp_stdio_servers = {
          MCPServerStdioExport{
              .provider_ref = make_provider_ref(
                  plugin_id,
                  std::string("mcp.") + suffix,
                  source_revision),
              .server_id = std::string("server.") + suffix,
              .launch_spec_ref = std::string("launch://") + plugin_id + "/" + suffix,
              .trust_level = std::string("trusted-local"),
          },
      },
      .skill_bundles = {
          SkillBundleExport{
              .provider_ref = make_provider_ref(
                  plugin_id,
                  std::string("skill.") + suffix,
                  source_revision),
              .bundle_id = std::string("bundle.") + suffix,
              .asset_root_ref = std::string("asset://") + plugin_id + "/" + suffix,
              .dialect_ref = std::string("internal.v1"),
          },
      },
  };
}

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
        "validation is not used by ToolPluginLifecycleBridgeIntegrationTest",
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

void test_lifecycle_adapter_syncs_active_plugins_and_reacts_to_events() {
  auto plugin_manager = std::make_shared<FakePluginManager>();
  auto bridge = std::make_shared<dasall::tools::bridge::PluginExtensionBridge>();

  std::map<std::string, dasall::tools::plugin::ToolPluginExtensionCatalog> catalogs_by_plugin{
      {"plugin.alpha", make_catalog("plugin.alpha", "rev-a", "alpha")},
  };
  std::map<std::string, dasall::tools::plugin::ToolPluginExtensionCatalog> catalogs_by_handle{
      {"handle://plugin.beta", make_catalog("plugin.beta", "rev-b", "beta")},
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
      });

  plugin_manager->set_active_plugins(
      {make_descriptor("plugin.alpha", dasall::infra::plugin::PluginStatus::Active),
       make_descriptor("plugin.disabled", dasall::infra::plugin::PluginStatus::Disabled)},
      false);

  assert_equal(1,
               static_cast<int>(lifecycle_bridge.synchronize_active_plugins()),
               "active set sync should publish only visible plugins with resolvable catalogs");

  auto snapshot = bridge->snapshot();
  assert_true(snapshot->builtin_providers_by_source.find("plugin:plugin.alpha") !=
                  snapshot->builtin_providers_by_source.end(),
              "active plugin sync should publish the alpha builtin provider view");
  assert_true(snapshot->mcp_launch_specs_by_source.find("plugin:plugin.alpha") !=
                  snapshot->mcp_launch_specs_by_source.end(),
              "active plugin sync should publish the alpha mcp launch spec view");
  assert_true(snapshot->skill_assets_by_source.find("plugin:plugin.alpha") !=
                  snapshot->skill_assets_by_source.end(),
              "active plugin sync should publish the alpha skill asset view");
  assert_true(snapshot->builtin_providers_by_source.find("plugin:plugin.disabled") ==
                  snapshot->builtin_providers_by_source.end(),
              "disabled plugins must not become visible through the lifecycle adapter");

  const auto beta_load = dasall::infra::plugin::PluginLoadResult::success(
      "plugin.beta",
      dasall::infra::plugin::PluginOperationPhase::Load,
      "handle://plugin.beta",
      "audit://plugin.beta/load");
  assert_true(lifecycle_bridge.on_plugin_loaded(beta_load),
              "load events with a resolvable handle should publish plugin exports");

  snapshot = bridge->snapshot();
  assert_true(snapshot->builtin_providers_by_source.find("plugin:plugin.beta") !=
                  snapshot->builtin_providers_by_source.end(),
              "load events should publish beta builtin provider views");
  assert_true(snapshot->mcp_launch_specs_by_source.find("plugin:plugin.beta") !=
                  snapshot->mcp_launch_specs_by_source.end(),
              "load events should publish beta mcp launch specs");
  assert_true(snapshot->skill_assets_by_source.find("plugin:plugin.beta") !=
                  snapshot->skill_assets_by_source.end(),
              "load events should publish beta skill asset refs");

  const auto alpha_unload = dasall::infra::plugin::PluginUnloadResult::success(
      "plugin.alpha",
      "audit://plugin.alpha/unload");
  assert_true(lifecycle_bridge.on_plugin_unloaded(alpha_unload),
              "unload events should revoke the matching plugin source");

  snapshot = bridge->snapshot();
  assert_true(snapshot->builtin_providers_by_source.find("plugin:plugin.alpha") ==
                  snapshot->builtin_providers_by_source.end(),
              "unload events should remove alpha builtin provider views");
  assert_true(snapshot->builtin_providers_by_source.find("plugin:plugin.beta") !=
                  snapshot->builtin_providers_by_source.end(),
              "unload events must not remove other plugin sources");

  plugin_manager->set_active_plugins(
      {make_descriptor("plugin.beta", dasall::infra::plugin::PluginStatus::Active)},
      true);
  assert_equal(0,
               static_cast<int>(lifecycle_bridge.synchronize_active_plugins()),
               "safe mode sync should revoke visible plugin sources instead of publishing them");

  snapshot = bridge->snapshot();
  assert_true(snapshot->builtin_providers_by_source.empty(),
              "safe mode sync should revoke builtin provider views");
  assert_true(snapshot->mcp_launch_specs_by_source.empty(),
              "safe mode sync should revoke mcp launch spec views");
  assert_true(snapshot->skill_assets_by_source.empty(),
              "safe mode sync should revoke skill asset views");
}

void test_invalid_catalog_is_isolated_by_lifecycle_adapter() {
  auto plugin_manager = std::make_shared<FakePluginManager>();
  auto bridge = std::make_shared<dasall::tools::bridge::PluginExtensionBridge>();

  auto invalid_catalog = make_catalog("plugin.bad", "rev-a", "bad");
  invalid_catalog.skill_bundles.front().provider_ref.plugin_id = "plugin.other";

  dasall::tools::bridge::ToolPluginLifecycleBridge lifecycle_bridge(
      plugin_manager,
      bridge,
      [invalid_catalog](std::string_view plugin_id, std::string_view handle_ref)
          -> std::optional<dasall::tools::plugin::ToolPluginExtensionCatalog> {
        static_cast<void>(plugin_id);
        static_cast<void>(handle_ref);
        return invalid_catalog;
      });

  const auto bad_load = dasall::infra::plugin::PluginLoadResult::success(
      "plugin.bad",
      dasall::infra::plugin::PluginOperationPhase::Load,
      "handle://plugin.bad",
      "audit://plugin.bad/load");
  assert_true(!lifecycle_bridge.on_plugin_loaded(bad_load),
              "invalid plugin export catalogs must be isolated at the lifecycle adapter boundary");
  assert_equal(0,
               static_cast<int>(bridge->snapshot()->revision),
               "invalid catalogs must not partially mutate the bridge snapshot");
}

}  // namespace

int main() {
  try {
    test_lifecycle_adapter_syncs_active_plugins_and_reacts_to_events();
    test_invalid_catalog_is_isolated_by_lifecycle_adapter();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}