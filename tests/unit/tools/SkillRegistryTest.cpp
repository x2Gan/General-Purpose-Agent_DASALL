#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "skills/SkillRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::skills::SkillSpecAsset make_asset(
    std::string source_key,
    std::string skill_id,
    std::vector<std::string> intent_patterns,
    std::vector<std::string> profile_constraints,
    std::vector<std::string> tags) {
  return dasall::tools::skills::SkillSpecAsset{
      .skill_id = std::move(skill_id),
      .source_key = std::move(source_key),
      .asset_ref = std::string("skills/specs/") +
                   (intent_patterns.empty() ? std::string("sample") : intent_patterns.front()),
      .version = "1",
      .name = "runtime-incident",
      .description = "Diagnose runtime incidents with a bounded tool allowlist.",
      .intent_patterns = std::move(intent_patterns),
      .tags = std::move(tags),
      .allowed_tools = {"runtime.inspect_status", "runtime.collect_logs"},
      .profile_constraints = std::move(profile_constraints),
      .required_domains = {"runtime"},
      .workflow_template_ref = "skills/workflows/runtime-incident-triage.workflow.yaml",
      .prompt_bundle_ref = std::nullopt,
      .eval_suite_ref = "skills/evals/runtime-incident-triage.eval.yaml",
      .fallback_mode = "builtin-summary",
  };
}

void test_register_list_and_upsert_assets() {
  using dasall::tools::skills::SkillRegistry;

  SkillRegistry registry;
  assert_equal(0, static_cast<int>(registry.list_assets().size()),
               "list_assets should be empty before registration");

  auto internal_asset = make_asset(
      "internal:skills/specs",
      "internal.runtime-incident-triage",
      {"diagnose runtime incident", "investigate runtime failure"},
      {"cloud_full", "desktop_full"},
      {"runtime", "diagnostics"});
  auto plugin_asset = make_asset(
      "plugin:ops-bundle",
      "plugin.runtime-audit",
      {"audit runtime changes"},
      {"cloud_full"},
      {"runtime", "audit"});

  assert_true(registry.register_asset(internal_asset),
              "register_asset should accept a valid internal normalized skill asset");
  assert_true(registry.register_asset(plugin_asset),
              "register_asset should accept a valid plugin-delivered normalized skill asset");

  assert_equal(2, static_cast<int>(registry.snapshot()->revision),
               "each successful registration should advance the snapshot revision");
  assert_equal(2, static_cast<int>(registry.list_assets().size()),
               "list_assets should expose assets from independent sources");

  internal_asset.description = "Updated runtime incident diagnosis guidance.";
  internal_asset.allowed_tools.push_back("knowledge.summarize_findings");

  assert_true(registry.register_asset(internal_asset),
              "register_asset should upsert an existing skill by source + skill_id");

  const auto assets = registry.list_assets();
  assert_equal(2, static_cast<int>(assets.size()),
               "upserting an asset must not duplicate the flattened view");

  const auto updated = std::find_if(
      assets.begin(),
      assets.end(),
      [](const dasall::tools::skills::SkillSpecAsset& asset) {
        return asset.skill_id == "internal.runtime-incident-triage";
      });
  assert_true(updated != assets.end(),
              "list_assets should still contain the updated internal skill");
  assert_equal(std::string("Updated runtime incident diagnosis guidance."),
               updated->description,
               "upserted assets should expose the latest payload");
  assert_equal(3, static_cast<int>(registry.snapshot()->revision),
               "upserting an asset should publish a new registry snapshot");
}

void test_match_and_revoke_source() {
  using dasall::tools::skills::SkillMatchQuery;
  using dasall::tools::skills::SkillRegistry;

  SkillRegistry registry;
  assert_true(
      registry.register_asset(make_asset(
          "internal:skills/specs",
          "internal.runtime-incident-triage",
          {"diagnose runtime incident", "investigate runtime failure"},
          {"cloud_full", "desktop_full"},
          {"runtime", "diagnostics"})),
      "precondition: internal skill asset registration should succeed");
  assert_true(
      registry.register_asset(make_asset(
          "plugin:ops-bundle",
          "plugin.runtime-audit",
          {"audit runtime changes"},
          {"cloud_full"},
          {"runtime", "audit"})),
      "precondition: plugin skill asset registration should succeed");

  const auto match = registry.match_intent(SkillMatchQuery{
      .intent_text = "diagnose runtime incident after deploy",
      .tags = {"runtime"},
      .profile_id = std::string("cloud_full"),
  });

  assert_true(match.matched, "match_intent should return the best matching skill asset");
  assert_true(match.asset.has_value(), "matched results should carry the selected asset");
  assert_equal(std::string("internal.runtime-incident-triage"), match.asset->skill_id,
               "runtime incident queries should select the internal incident skill");
  assert_equal(std::string("skill.match.selected"), match.reason_code,
               "successful match should expose the selected reason code");
  assert_true(match.score > 0U, "successful match should expose a positive score");
  assert_true(
      std::find(match.matched_terms.begin(), match.matched_terms.end(), "runtime") !=
          match.matched_terms.end(),
      "matched_terms should preserve the normalized overlap evidence");

  assert_true(registry.revoke_source("plugin:ops-bundle"),
              "revoke_source should delete all assets owned by the selected source");
  assert_equal(1, static_cast<int>(registry.list_assets().size()),
               "source revoke should leave unrelated sources intact");
  assert_true(!registry.revoke_source("plugin:missing"),
              "revoke_source should fail closed when the source does not exist");
}

}  // namespace

int main() {
  try {
    test_register_list_and_upsert_assets();
    test_match_and_revoke_source();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}