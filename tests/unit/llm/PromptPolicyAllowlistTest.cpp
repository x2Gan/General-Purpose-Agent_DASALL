#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "prompt/PromptComposeResult.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/PromptPolicy.h"

namespace {

using dasall::contracts::PromptComposeResult;
using dasall::llm::prompt::PromptPolicy;
using dasall::llm::prompt::PromptPolicyDisposition;
using dasall::llm::prompt::PromptPolicyInput;

PromptComposeResult make_compose_result() {
  return PromptComposeResult{
      .messages = std::vector<std::string>{
          "system: Follow the planner release instructions",
          "user: Summarize the current diagnostics state",
      },
      .selected_prompt_id = "planner",
      .selected_version = "2026.04.12",
      .estimated_tokens = 32,
      .pruned_sections = std::nullopt,
      .composition_warnings = std::nullopt,
  };
}

void test_prompt_policy_fails_closed_when_selected_trusted_source_is_missing() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptPolicy policy;
  assert_true(policy.init({.default_allowed_releases = {"stable"},
                           .default_trusted_sources = {"profiles"},
                           .deny_on_missing_allowlist = true}),
              "PromptPolicy should initialize with explicit fail-closed defaults");

  const auto decision = policy.evaluate(
      make_compose_result(),
      PromptPolicyInput{
          .profile_id = "desktop_full",
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all"},
          .render_budget_tokens = 512U,
          .active_scene = "ops_diagnosis",
          .active_persona = "default_planner",
          .selected_release_scope = "stable",
          .selected_trusted_source = std::string(),
          .visible_tools = {"builtin.plan"},
      });

  assert_equal(static_cast<int>(PromptPolicyDisposition::Deny),
               static_cast<int>(decision.disposition),
               "PromptPolicy should deny when trusted source evidence is missing");
  assert_true(decision.reason == "trusted_source_missing",
              "PromptPolicy should keep a fail-closed reason for missing trusted source evidence");
}

void test_prompt_policy_denies_prompt_release_outside_allowlist() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptPolicy policy;
  assert_true(policy.init({.default_allowed_releases = {},
                           .default_trusted_sources = {"profiles"},
                           .deny_on_missing_allowlist = true}),
              "PromptPolicy should initialize before allowlist denial checks");

  const auto decision = policy.evaluate(
      make_compose_result(),
      PromptPolicyInput{
          .profile_id = "desktop_full",
          .allowed_prompt_releases = {"canary"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all"},
          .render_budget_tokens = 512U,
          .active_scene = "ops_diagnosis",
          .active_persona = "default_planner",
          .selected_release_scope = "stable",
          .selected_trusted_source = "profiles",
          .visible_tools = {"builtin.plan"},
      });

  assert_equal(static_cast<int>(PromptPolicyDisposition::Deny),
               static_cast<int>(decision.disposition),
               "PromptPolicy should deny prompts whose release scope is not allowlisted");
  assert_true(decision.reason == "prompt_release_not_allowed",
              "PromptPolicy should report allowlist denial explicitly");
}

void test_prompt_policy_denies_when_exact_release_version_is_not_allowed() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptPolicy policy;
  assert_true(policy.init({.default_allowed_releases = {},
                           .default_trusted_sources = {"profiles"},
                           .deny_on_missing_allowlist = true}),
              "PromptPolicy should initialize before exact release checks");

  const auto decision = policy.evaluate(
      make_compose_result(),
      PromptPolicyInput{
          .profile_id = "desktop_full",
          .allowed_prompt_releases = {"planner@2026.04.11"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all"},
          .render_budget_tokens = 512U,
          .active_scene = "ops_diagnosis",
          .active_persona = "default_planner",
          .selected_release_scope = "stable",
          .selected_trusted_source = "profiles",
          .visible_tools = {"builtin.plan"},
      });

  assert_equal(static_cast<int>(PromptPolicyDisposition::Deny),
               static_cast<int>(decision.disposition),
               "PromptPolicy should deny exact prompt releases that are not explicitly allowed");
  assert_true(decision.reason == "prompt_release_not_allowed",
              "PromptPolicy should preserve the same deny reason for exact release mismatch");
}

}  // namespace

int main() {
  try {
    test_prompt_policy_fails_closed_when_selected_trusted_source_is_missing();
    test_prompt_policy_denies_prompt_release_outside_allowlist();
    test_prompt_policy_denies_when_exact_release_version_is_not_allowed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}