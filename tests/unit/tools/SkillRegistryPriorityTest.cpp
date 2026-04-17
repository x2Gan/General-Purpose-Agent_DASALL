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
    std::string skill_id,
    std::vector<std::string> intent_patterns,
    std::vector<std::string> profile_constraints,
    std::vector<std::string> tags) {
  return dasall::tools::skills::SkillSpecAsset{
      .skill_id = std::move(skill_id),
      .source_key = "internal:skills/specs",
      .asset_ref = "skills/specs/runtime-incident-triage.skill.yaml",
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

void test_highest_scoring_skill_wins() {
  using dasall::tools::skills::SkillMatchQuery;
  using dasall::tools::skills::SkillRegistry;

  SkillRegistry registry;
  assert_true(
      registry.register_asset(make_asset(
          "internal.runtime-overview",
          {"runtime status"},
          {"desktop_full"},
          {"runtime"})),
      "precondition: general runtime skill should register successfully");
  assert_true(
      registry.register_asset(make_asset(
          "internal.runtime-incident-triage",
          {"diagnose runtime incident", "investigate runtime failure"},
          {"desktop_full"},
          {"runtime", "diagnostics"})),
      "precondition: specific incident skill should register successfully");

  const auto match = registry.match_intent(SkillMatchQuery{
      .intent_text = "diagnose runtime incident",
      .tags = {"runtime"},
      .profile_id = std::string("desktop_full"),
  });

  assert_true(match.matched, "specific runtime incident query should match a skill");
  assert_true(match.asset.has_value(), "matched result should include the selected asset");
  assert_equal(std::string("internal.runtime-incident-triage"), match.asset->skill_id,
               "the higher scoring incident skill should win over the generic runtime skill");
}

void test_profile_filter_is_fail_closed() {
  using dasall::tools::skills::SkillMatchQuery;
  using dasall::tools::skills::SkillRegistry;

  SkillRegistry registry;
  assert_true(
      registry.register_asset(make_asset(
          "internal.runtime-incident-triage",
          {"diagnose runtime incident"},
          {"cloud_full"},
          {"runtime", "diagnostics"})),
      "precondition: constrained runtime skill should register successfully");

  const auto filtered = registry.match_intent(SkillMatchQuery{
      .intent_text = "diagnose runtime incident",
      .tags = {"runtime"},
      .profile_id = std::string("edge_minimal"),
  });

  assert_true(!filtered.matched, "profile mismatch should not produce a matched skill");
  assert_equal(std::string("skill.match.profile_filtered"), filtered.reason_code,
               "profile mismatch should return the fail-closed profile_filtered reason");
}

}  // namespace

int main() {
  try {
    test_highest_scoring_skill_wins();
    test_profile_filter_is_fail_closed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}