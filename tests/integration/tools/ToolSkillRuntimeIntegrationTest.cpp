#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "skills/PluginSkillBundleImporter.h"
#include "skills/SkillRegistry.h"
#include "skills/SkillRuntime.h"
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

[[nodiscard]] dasall::tools::ToolPolicyView make_policy_view() {
  return dasall::tools::ToolPolicyView{
      .effective_profile_id = "desktop_full",
      .safe_mode_enabled = true,
      .high_risk_confirmation_required = true,
      .audit_level = "full",
      .allowed_tool_domains = {"runtime", "knowledge"},
      .tool_visibility_rules = {"builtin:all"},
  };
}

[[nodiscard]] dasall::tools::bridge::SkillAssetRef make_internal_bundle_ref() {
  return dasall::tools::bridge::SkillAssetRef{
      .provider_ref = dasall::tools::plugin::ToolPluginProviderRef{
          .plugin_id = "plugin.skill-runtime",
          .export_key = "skills.internal",
          .source_revision = "rev-1",
      },
      .source_key = "plugin:plugin.skill-runtime",
      .bundle_id = "bundle.internal",
      .asset_root_ref = "skills/specs",
      .dialect_ref = std::string("dasall.skill.v1"),
  };
}

void test_internal_skill_asset_import_match_and_instantiate() {
  dasall::tools::skills::PluginSkillBundleImporter importer(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = false,
          .project_root = project_root(),
      });
  dasall::tools::skills::SkillRegistry registry;
  dasall::tools::skills::SkillRuntime runtime(project_root());

  const auto import_result = importer.import_bundle(make_internal_bundle_ref());
  assert_equal(2, static_cast<int>(import_result.imported_assets.size()),
               "integration path should import the canonical internal skill bundle assets");
  const auto asset_it = std::find_if(import_result.imported_assets.begin(),
      import_result.imported_assets.end(),
      [](const auto& a) { return a.asset_ref.find("runtime-incident-triage") != std::string::npos; });
  assert_true(asset_it != import_result.imported_assets.end(),
              "internal bundle should contain the runtime-incident-triage skill asset");
  assert_true(registry.register_asset(*asset_it),
              "imported internal skill asset should register into SkillRegistry");

  const auto match_result = registry.match_intent(
      dasall::tools::skills::SkillMatchQuery{
          .intent_text = "diagnose runtime incident after deploy",
          .tags = {"runtime"},
          .profile_id = std::string("desktop_full"),
      });
  assert_true(match_result.matched,
              "registered internal skill asset should match the runtime incident query");

  const auto instantiate_result = runtime.instantiate(match_result, make_policy_view());
  assert_true(instantiate_result.instantiated,
              "matched internal skill asset should instantiate into a runtime workflow plan");
  assert_true(instantiate_result.instance.has_value(),
              "successful integration instantiation should return a SkillInstance");
  assert_true(instantiate_result.workflow_plan.has_value(),
              "successful integration instantiation should return a WorkflowPlan");
  assert_equal(asset_it->skill_id,
               instantiate_result.instance->asset.skill_id,
               "runtime instance should preserve the imported skill id");
  assert_equal(std::string("skill.runtime-incident-triage"),
               instantiate_result.workflow_plan->workflow_id,
               "workflow plan should bind the canonical runtime incident workflow");
  assert_equal(3, static_cast<int>(instantiate_result.workflow_plan->steps.size()),
               "integration workflow plan should materialize all three skill steps");
  assert_equal(2, static_cast<int>(instantiate_result.workflow_plan->step_output_mapping.size()),
               "integration workflow plan should preserve the output binding edges");
  assert_true(runtime.release_instance(instantiate_result.instance->instance_id),
              "integration runtime instance should be releasable after instantiation");
}

}  // namespace

int main() {
  try {
    test_internal_skill_asset_import_match_and_instantiate();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}