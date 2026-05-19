#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "bridge/ToolPluginExtensionConsumer.h"
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
  const auto catalog = make_skill_bundle_catalog();

  auto disabled_registry = std::make_shared<dasall::tools::skills::SkillRegistry>();
  dasall::tools::skills::PluginSkillBundleImporter disabled_importer(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = false,
          .project_root = project_root(),
      });
  dasall::tools::bridge::ToolPluginExtensionConsumer disabled_consumer(
    dasall::tools::bridge::ToolPluginExtensionConsumerDependencies{
      .registry = nullptr,
      .discovery = nullptr,
      .skill_registry = disabled_registry,
      .builtin_descriptor_resolver = {},
      .skill_bundle_importer = [&disabled_importer](const auto& skill_asset_ref) {
      return disabled_importer.import_bundle(skill_asset_ref);
      },
    });
  assert_true(disabled_consumer.on_plugin_loaded(catalog),
        "disabled external dialect import should stay fail-closed without rejecting the plugin source");
  assert_true(disabled_registry->list_assets().empty(),
        "external dialect bundle should stay behind the module-local feature flag");

  auto enabled_registry = std::make_shared<dasall::tools::skills::SkillRegistry>();
  dasall::tools::skills::PluginSkillBundleImporter enabled_importer(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = true,
          .project_root = project_root(),
      });
  dasall::tools::bridge::ToolPluginExtensionConsumer enabled_consumer(
    dasall::tools::bridge::ToolPluginExtensionConsumerDependencies{
      .registry = nullptr,
      .discovery = nullptr,
      .skill_registry = enabled_registry,
      .builtin_descriptor_resolver = {},
      .skill_bundle_importer = [&enabled_importer](const auto& skill_asset_ref) {
      return enabled_importer.import_bundle(skill_asset_ref);
      },
    });
  assert_true(enabled_consumer.on_plugin_loaded(catalog),
        "enabled external dialect import should register normalized plugin skill assets through the consumer path");
  assert_equal(2, static_cast<int>(enabled_registry->list_assets().size()),
         "enabled external dialect bundle should normalize into two skill assets");
  const auto reg_it = std::find_if(
    enabled_registry->list_assets().begin(),
    enabled_registry->list_assets().end(),
    [](const auto& a) { return a.name == "runtime-incident"; });
  assert_true(reg_it != enabled_registry->list_assets().end(),
        "enabled external dialect bundle should contain the runtime-incident skill asset");
  assert_equal(std::string(kSourceKey), enabled_registry->list_assets().front().source_key,
         "registered plugin skill asset should preserve the plugin source key");

  assert_true(enabled_consumer.on_plugin_unloaded(kPluginId),
        "plugin unload should revoke all plugin-owned skill assets through the consumer path");
  assert_true(enabled_registry->list_assets().empty(),
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