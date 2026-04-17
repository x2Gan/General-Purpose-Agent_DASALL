#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "bridge/PluginExtensionBridge.h"
#include "plugin/IToolPluginProvider.h"
#include "skills/PluginSkillBundleImporter.h"
#include "skills/SkillRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kPluginId = "plugin.skill-bundle";
constexpr std::string_view kSourceKey = "plugin:plugin.skill-bundle";

[[nodiscard]] std::filesystem::path project_root() {
  auto root = std::filesystem::path(__FILE__);
  for (int level = 0; level < 4; ++level) {
    root = root.parent_path();
  }

  return root;
}

[[nodiscard]] bool has_reason_code(
    const std::vector<dasall::tools::skills::SkillImportDiagnostic>& diagnostics,
    const std::string& reason_code) {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [&reason_code](const auto& diagnostic) {
                       return diagnostic.reason_code == reason_code;
                     });
}

[[nodiscard]] dasall::tools::plugin::ToolPluginExtensionCatalog make_skill_bundle_catalog() {
  return dasall::tools::plugin::ToolPluginExtensionCatalog{
      .payload_kinds = {dasall::tools::plugin::ToolPluginPayloadKind::skill_bundle},
      .builtin_tool_providers = {},
      .mcp_stdio_servers = {},
      .skill_bundles = {
          dasall::tools::plugin::SkillBundleExport{
              .provider_ref = dasall::tools::plugin::ToolPluginProviderRef{
                  .plugin_id = std::string(kPluginId),
                  .export_key = "skills.github.runtime",
                  .source_revision = "rev-1",
              },
              .bundle_id = "bundle.github-runtime",
              .asset_root_ref = "skills/external_dialects/github",
              .dialect_ref = std::string("github.skills"),
          },
      },
  };
}

void test_plugin_skill_bundle_feature_flag_and_source_revoke() {
  dasall::tools::bridge::PluginExtensionBridge bridge;
  dasall::tools::skills::SkillRegistry registry;

  assert_true(bridge.on_plugin_loaded(make_skill_bundle_catalog()),
              "plugin bridge should accept a valid skill bundle catalog");

  const auto snapshot = bridge.snapshot();
  const auto source_it = snapshot->skill_assets_by_source.find(std::string(kSourceKey));
  assert_true(source_it != snapshot->skill_assets_by_source.end(),
              "plugin bridge should publish skill asset refs under the plugin source key");
  assert_equal(1, static_cast<int>(source_it->second.size()),
               "plugin bridge should publish exactly one skill asset ref for the sample bundle");

  const auto& skill_asset_ref = source_it->second.front();

  dasall::tools::skills::PluginSkillBundleImporter disabled_importer(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = false,
          .project_root = project_root(),
      });
  const auto disabled_result = disabled_importer.import_bundle(skill_asset_ref);
  assert_equal(0, static_cast<int>(disabled_result.imported_assets.size()),
               "external dialect bundle should stay behind the module-local feature flag");
  assert_true(has_reason_code(disabled_result.diagnostics, "skill.importer.feature_disabled"),
              "disabled external dialect import should emit the feature_disabled diagnostic");

  dasall::tools::skills::PluginSkillBundleImporter enabled_importer(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = true,
          .project_root = project_root(),
      });
  const auto enabled_result = enabled_importer.import_bundle(skill_asset_ref);
  assert_equal(2, static_cast<int>(enabled_result.imported_assets.size()),
               "enabled external dialect bundle should normalize into two skill assets");
  const auto reg_it = std::find_if(enabled_result.imported_assets.begin(),
      enabled_result.imported_assets.end(),
      [](const auto& a) { return a.name == "runtime-incident"; });
  assert_true(reg_it != enabled_result.imported_assets.end(),
              "enabled external dialect bundle should contain the runtime-incident skill asset");
  assert_true(registry.register_asset(*reg_it),
              "normalized plugin skill bundle should register into SkillRegistry");
  assert_equal(1, static_cast<int>(registry.list_assets().size()),
               "registered plugin skill bundle should appear in the registry list view");
  assert_equal(std::string(kSourceKey), registry.list_assets().front().source_key,
               "registered plugin skill asset should preserve the plugin source key");

  assert_true(bridge.on_plugin_unloaded(kPluginId),
              "plugin unload should revoke the bridge-side skill asset refs");
  assert_true(registry.revoke_source(kSourceKey),
              "consumer-side registry should revoke all plugin-owned skill assets on unload");
  assert_true(registry.list_assets().empty(),
              "registry should be empty after source-scoped plugin skill revoke");
}

}  // namespace

int main() {
  try {
    test_plugin_skill_bundle_feature_flag_and_source_revoke();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}