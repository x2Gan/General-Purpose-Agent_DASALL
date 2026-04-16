#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "bridge/PluginExtensionBridge.h"
#include "plugin/IToolPluginProvider.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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
    const std::string& provider_suffix) {
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
                  std::string("builtin.") + provider_suffix,
                  source_revision),
              .provider_handle_ref = std::string("provider://") + plugin_id + "/" + provider_suffix,
          },
      },
      .mcp_stdio_servers = {
          MCPServerStdioExport{
              .provider_ref = make_provider_ref(
                  plugin_id,
                  std::string("mcp.") + provider_suffix,
                  source_revision),
              .server_id = std::string("server.") + provider_suffix,
              .launch_spec_ref = std::string("launch://") + plugin_id + "/" + provider_suffix,
              .trust_level = std::string("trusted-local"),
          },
      },
      .skill_bundles = {
          SkillBundleExport{
              .provider_ref = make_provider_ref(
                  plugin_id,
                  std::string("skill.") + provider_suffix,
                  source_revision),
              .bundle_id = std::string("bundle.") + provider_suffix,
              .asset_root_ref = std::string("asset://") + plugin_id + "/" + provider_suffix,
              .dialect_ref = std::string("internal.v1"),
          },
      },
  };
}

void test_plugin_load_and_unload_publish_source_scoped_snapshot() {
  using dasall::tools::bridge::PluginExtensionBridge;

  PluginExtensionBridge bridge;
  const auto initial_catalog = make_catalog("plugin.echo", "rev-a", "alpha");

  assert_true(bridge.on_plugin_loaded(initial_catalog),
              "on_plugin_loaded should accept a valid single-plugin extension catalog");

  auto snapshot = bridge.snapshot();
  const std::string source_key = "plugin:plugin.echo";

  assert_equal(1, static_cast<int>(snapshot->revision),
               "the first successful publish should advance the bridge revision");
  assert_equal(1, static_cast<int>(snapshot->builtin_providers_by_source.size()),
               "valid load should publish builtin providers under one source key");
  assert_equal(1, static_cast<int>(snapshot->mcp_launch_specs_by_source.size()),
               "valid load should publish mcp launch specs under one source key");
  assert_equal(1, static_cast<int>(snapshot->skill_assets_by_source.size()),
               "valid load should publish skill asset refs under one source key");
  assert_equal(std::string("provider://plugin.echo/alpha"),
               snapshot->builtin_providers_by_source.at(source_key).front().provider_handle_ref,
               "builtin delta should preserve the provider handle reference");
  assert_equal(std::string("server.alpha"),
               snapshot->mcp_launch_specs_by_source.at(source_key).front().server_id,
               "mcp delta should preserve the launch server id");
  assert_equal(std::string("bundle.alpha"),
               snapshot->skill_assets_by_source.at(source_key).front().bundle_id,
               "skill delta should preserve the bundle id");

  assert_true(bridge.on_plugin_loaded(make_catalog("plugin.echo", "rev-b", "beta")),
              "reloading the same plugin should reconcile the source-owned export batch");

  snapshot = bridge.snapshot();
  assert_equal(2, static_cast<int>(snapshot->revision),
               "reloading the same plugin should publish a new bridge revision");
  assert_equal(std::string("provider://plugin.echo/beta"),
               snapshot->builtin_providers_by_source.at(source_key).front().provider_handle_ref,
               "source reconcile should replace the previous builtin provider ref");
  assert_equal(std::string("server.beta"),
               snapshot->mcp_launch_specs_by_source.at(source_key).front().server_id,
               "source reconcile should replace the previous launch spec batch");
  assert_equal(std::string("bundle.beta"),
               snapshot->skill_assets_by_source.at(source_key).front().bundle_id,
               "source reconcile should replace the previous skill asset batch");

  assert_true(bridge.on_plugin_unloaded("plugin.echo"),
              "plugin unload should revoke all source-owned bridge deltas");

  snapshot = bridge.snapshot();
  assert_true(snapshot->builtin_providers_by_source.find(source_key) ==
                  snapshot->builtin_providers_by_source.end(),
              "plugin unload should remove builtin providers for the unloaded source");
  assert_true(snapshot->mcp_launch_specs_by_source.find(source_key) ==
                  snapshot->mcp_launch_specs_by_source.end(),
              "plugin unload should remove launch specs for the unloaded source");
  assert_true(snapshot->skill_assets_by_source.find(source_key) ==
                  snapshot->skill_assets_by_source.end(),
              "plugin unload should remove skill assets for the unloaded source");
}

void test_invalid_plugin_catalog_fails_closed() {
  using dasall::tools::bridge::PluginExtensionBridge;

  PluginExtensionBridge bridge;
  auto invalid_catalog = make_catalog("plugin.bad", "rev-a", "alpha");
  invalid_catalog.skill_bundles.front().provider_ref.plugin_id = std::string("plugin.other");

  assert_true(!bridge.on_plugin_loaded(invalid_catalog),
              "mixed plugin ids inside one extension catalog must be rejected");
  assert_equal(0, static_cast<int>(bridge.snapshot()->revision),
               "invalid plugin catalogs must not partially mutate the bridge snapshot");

  auto missing_payload_kind = make_catalog("plugin.bad", "rev-b", "beta");
  missing_payload_kind.payload_kinds.pop_back();
  assert_true(!bridge.on_plugin_loaded(missing_payload_kind),
              "payload kind mismatches must be rejected instead of silently inferred");
}

}  // namespace

int main() {
  try {
    test_plugin_load_and_unload_publish_source_scoped_snapshot();
    test_invalid_plugin_catalog_fails_closed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}