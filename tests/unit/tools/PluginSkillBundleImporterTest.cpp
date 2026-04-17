#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "bridge/PluginExtensionBridge.h"
#include "plugin/IToolPluginProvider.h"
#include "skills/PluginSkillBundleImporter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] std::filesystem::path project_root() {
  auto root = std::filesystem::path(__FILE__);
  for (int level = 0; level < 4; ++level) {
    root = root.parent_path();
  }

  return root;
}

[[nodiscard]] dasall::tools::bridge::SkillAssetRef make_skill_asset_ref(
    const std::string& source_key,
    const std::string& bundle_id,
    const std::string& asset_root_ref,
    std::optional<std::string> dialect_ref) {
  return dasall::tools::bridge::SkillAssetRef{
      .provider_ref = dasall::tools::plugin::ToolPluginProviderRef{
          .plugin_id = "plugin.runtime",
          .export_key = "skills.runtime",
          .source_revision = "rev-1",
      },
      .source_key = source_key,
      .bundle_id = bundle_id,
      .asset_root_ref = asset_root_ref,
      .dialect_ref = std::move(dialect_ref),
  };
}

[[nodiscard]] dasall::tools::skills::PluginSkillBundleImporter make_importer(bool enabled) {
  return dasall::tools::skills::PluginSkillBundleImporter(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = enabled,
          .project_root = project_root(),
      });
}

void test_internal_bundle_import_reads_normalized_skill_assets() {
  const auto importer = make_importer(false);
  const auto result = importer.import_bundle(make_skill_asset_ref(
      "plugin:runtime.bundle",
      "bundle.internal",
      "skills/specs",
      std::string("dasall.skill.v1")));

  assert_equal(1, static_cast<int>(result.imported_assets.size()),
               "normalized internal plugin bundle should import the canonical .skill.yaml asset");
  const auto& asset = result.imported_assets.front();
  assert_equal(std::string("plugin:runtime.bundle"), asset.source_key,
               "internal plugin bundle import should preserve the plugin source key");
  assert_equal(std::string("skills/specs/runtime-incident-triage.skill.yaml"), asset.asset_ref,
               "normalized internal bundle should expose the project-root-relative asset ref");
  assert_equal(std::string("skills/workflows/runtime-incident-triage.workflow.yaml"),
               asset.workflow_template_ref,
               "normalized internal bundle should preserve workflow refs from the checked-in sample");
}

void test_external_bundle_import_delegates_to_external_importer() {
  const auto importer = make_importer(true);
  const auto result = importer.import_bundle(make_skill_asset_ref(
      "plugin:runtime.bundle",
      "bundle.github",
      "skills/external_dialects/github",
      std::string("github.skills")));

  assert_equal(1, static_cast<int>(result.imported_assets.size()),
               "external plugin bundle should delegate to ExternalSkillImporter when the feature flag is enabled");
  const auto& asset = result.imported_assets.front();
  assert_equal(std::string("plugin:runtime.bundle"), asset.source_key,
               "delegated external bundle import should preserve the plugin source key");
  assert_equal(std::string("runtime-incident"), asset.name,
               "delegated external bundle import should normalize the GitHub-style fixture name");
}

}  // namespace

int main() {
  try {
    test_internal_bundle_import_reads_normalized_skill_assets();
    test_external_bundle_import_delegates_to_external_importer();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}