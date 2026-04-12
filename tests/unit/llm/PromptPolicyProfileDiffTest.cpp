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
          "system: planner release for canary diagnostics",
          "user: Summarize the latest runtime signals",
      },
      .selected_prompt_id = "planner",
      .selected_version = "2026.04.12",
      .estimated_tokens = 24,
      .pruned_sections = std::nullopt,
      .composition_warnings = std::nullopt,
  };
}

void test_prompt_policy_honors_profile_specific_allowlist_and_trusted_source_differences() {
  using dasall::tests::support::assert_equal;

  PromptPolicy policy;
  policy.init({.default_allowed_releases = {},
               .default_trusted_sources = {},
               .deny_on_missing_allowlist = true});

  const PromptComposeResult compose_result = make_compose_result();

  const PromptPolicyInput desktop_like_input{
      .profile_id = "desktop_full",
      .allowed_prompt_releases = {"stable", "canary"},
      .trusted_sources = {"profiles", "infra_config"},
      .tool_visibility_rules = {"builtin:all", "mcp:trusted"},
      .render_budget_tokens = 256U,
      .active_scene = "ops_diagnosis",
      .active_persona = "default_planner",
      .selected_release_scope = "canary",
      .selected_trusted_source = "infra_config",
      .visible_tools = {"builtin.plan", "mcp.read_only"},
  };

  const PromptPolicyInput edge_like_input{
      .profile_id = "edge_minimal",
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = {"profiles"},
      .tool_visibility_rules = {"builtin:essential"},
      .render_budget_tokens = 128U,
      .active_scene = "ops_diagnosis",
      .active_persona = "default_planner",
      .selected_release_scope = "canary",
      .selected_trusted_source = "infra_config",
      .visible_tools = {"builtin.plan"},
  };

  const auto desktop_decision = policy.evaluate(compose_result, desktop_like_input);
  const auto edge_decision = policy.evaluate(compose_result, edge_like_input);

  assert_equal(static_cast<int>(PromptPolicyDisposition::Allow),
               static_cast<int>(desktop_decision.disposition),
               "PromptPolicy should allow canary releases from infra_config under desktop/cloud style profiles");
  assert_equal(static_cast<int>(PromptPolicyDisposition::Deny),
               static_cast<int>(edge_decision.disposition),
               "PromptPolicy should deny the same prompt under stricter edge profiles");
}

}  // namespace

int main() {
  try {
    test_prompt_policy_honors_profile_specific_allowlist_and_trusted_source_differences();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}