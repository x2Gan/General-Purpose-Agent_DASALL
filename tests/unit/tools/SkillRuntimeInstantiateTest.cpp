#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

[[nodiscard]] dasall::tools::ToolPolicyView make_policy_view(
    std::string profile_id,
    std::vector<std::string> allowed_domains) {
  return dasall::tools::ToolPolicyView{
      .effective_profile_id = std::move(profile_id),
      .safe_mode_enabled = true,
      .high_risk_confirmation_required = true,
      .audit_level = "full",
      .allowed_tool_domains = std::move(allowed_domains),
      .tool_visibility_rules = {"builtin:all"},
  };
}

[[nodiscard]] dasall::tools::skills::SkillSpecAsset make_asset(
    std::vector<std::string> required_domains = {"runtime"}) {
  return dasall::tools::skills::SkillSpecAsset{
      .skill_id = "internal.runtime-incident-triage",
      .source_key = "internal:skills/specs",
      .asset_ref = "skills/specs/runtime-incident-triage.skill.yaml",
      .version = "1",
      .name = "runtime-incident-triage",
      .description = "Diagnose runtime incidents with a fixed read-mostly workflow.",
      .intent_patterns = {
          "diagnose runtime incident",
          "investigate runtime failure",
      },
      .tags = {"runtime", "diagnostics"},
      .allowed_tools = {
          "runtime.inspect_status",
          "runtime.collect_logs",
          "knowledge.summarize_findings",
      },
      .profile_constraints = {"desktop_full", "cloud_full"},
      .required_domains = std::move(required_domains),
      .workflow_template_ref = "skills/workflows/runtime-incident-triage.workflow.yaml",
      .prompt_bundle_ref = std::nullopt,
      .eval_suite_ref = "skills/evals/runtime-incident-triage.eval.yaml",
      .fallback_mode = "builtin-summary",
  };
}

[[nodiscard]] dasall::tools::skills::SkillMatchResult make_match_result(
    const dasall::tools::skills::SkillSpecAsset& asset) {
  return dasall::tools::skills::SkillMatchResult{
      .matched = true,
      .asset = asset,
      .reason_code = "skill.match.selected",
      .matched_terms = {"runtime", "incident"},
      .score = 24U,
  };
}

void test_instantiate_binds_workflow_and_tracks_instance() {
  dasall::tools::skills::SkillRuntime runtime(project_root());
  const auto result = runtime.instantiate(
      make_match_result(make_asset()),
      make_policy_view("desktop_full", {"runtime", "knowledge"}));

  assert_true(result.instantiated,
              "instantiate should succeed when policy allows every skill tool domain");
  assert_true(result.instance.has_value(),
              "successful instantiation should return a SkillInstance");
  assert_true(result.workflow_plan.has_value(),
              "successful instantiation should bind the workflow template");
  assert_equal(std::string("skill.runtime.instantiated"), result.reason_code,
               "successful instantiation should emit the instantiated reason code");
  assert_equal(std::string("builtin-summary"),
               result.fallback_strategy.value_or(std::string()),
               "instantiation should preserve the fallback strategy for later runtime decisions");
  assert_equal(std::string("skill.runtime-incident-triage"),
               result.workflow_plan->workflow_id,
               "workflow binding should load the canonical 036 workflow sample");
  assert_equal(3, static_cast<int>(result.workflow_plan->steps.size()),
               "workflow binding should materialize every configured step");
  assert_equal(2, static_cast<int>(result.workflow_plan->step_output_mapping.size()),
               "workflow binding should materialize every configured output binding");
  assert_true(!result.instance->instance_id.empty(),
              "successful instantiation should allocate a stable instance id");
  assert_true(runtime.release_instance(result.instance->instance_id),
              "release_instance should remove a stored active instance");
  assert_true(!runtime.release_instance(result.instance->instance_id),
              "releasing the same instance twice should fail closed");
}

void test_build_tool_allowlist_filters_denied_domains() {
  const dasall::tools::skills::SkillRuntime runtime(project_root());
  const auto allowlist = runtime.build_tool_allowlist(
      make_asset(),
      make_policy_view("desktop_full", {"runtime"}));

  assert_equal(2, static_cast<int>(allowlist.size()),
               "build_tool_allowlist should keep only tools whose domains remain policy-allowed");
  assert_true(
      std::find(allowlist.begin(), allowlist.end(), "runtime.inspect_status") !=
          allowlist.end(),
      "runtime inspect tool should remain in the allowlist");
  assert_true(
      std::find(allowlist.begin(), allowlist.end(), "knowledge.summarize_findings") ==
          allowlist.end(),
      "knowledge domain tool should be removed when the domain is not policy-allowed");
}

void test_policy_denial_reports_fallback_strategy() {
  dasall::tools::skills::SkillRuntime runtime(project_root());
  const auto result = runtime.instantiate(
      make_match_result(make_asset()),
      make_policy_view("desktop_full", {"runtime"}));

  assert_true(!result.instantiated,
              "instantiate should fail closed when policy removes part of the tool allowlist");
  assert_equal(std::string("skill.runtime.policy_denied"), result.reason_code,
               "policy-trimmed allowlist should produce the policy_denied reason code");
  assert_equal(std::string("builtin-summary"),
               result.fallback_strategy.value_or(std::string()),
               "policy denial should preserve the configured fallback strategy");
  assert_equal(1, static_cast<int>(result.denied_tools.size()),
               "policy denial should report the removed tool names");
  assert_equal(std::string("knowledge.summarize_findings"), result.denied_tools.front(),
               "policy denial should identify the exact denied tool");
}

}  // namespace

int main() {
  try {
    test_instantiate_binds_workflow_and_tracks_instance();
    test_build_tool_allowlist_filters_denied_domains();
    test_policy_denial_reports_fallback_strategy();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}