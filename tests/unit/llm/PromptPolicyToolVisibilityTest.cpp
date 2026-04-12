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

PromptComposeResult make_compose_result(std::vector<std::string> messages,
                                        std::int64_t estimated_tokens = 64,
                                        std::optional<std::vector<std::string>> warnings = std::nullopt) {
  return PromptComposeResult{
      .messages = std::move(messages),
      .selected_prompt_id = "planner",
      .selected_version = "2026.04.12",
      .estimated_tokens = estimated_tokens,
      .pruned_sections = std::nullopt,
      .composition_warnings = std::move(warnings),
  };
}

PromptPolicyInput make_policy_input(std::vector<std::string> tool_visibility_rules,
                                    std::vector<std::string> visible_tools,
                                    std::uint32_t render_budget_tokens) {
  return PromptPolicyInput{
      .profile_id = "desktop_full",
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = {"profiles"},
      .tool_visibility_rules = std::move(tool_visibility_rules),
      .render_budget_tokens = render_budget_tokens,
      .active_scene = "ops_diagnosis",
      .active_persona = "default_planner",
      .selected_release_scope = "stable",
      .selected_trusted_source = "profiles",
      .visible_tools = std::move(visible_tools),
  };
}

bool contains_value(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

void test_prompt_policy_emits_tool_visibility_patch_for_matching_rules() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptPolicy policy;
  assert_true(policy.init({.default_allowed_releases = {"stable"},
                           .default_trusted_sources = {"profiles"},
                           .deny_on_missing_allowlist = true}),
              "PromptPolicy should initialize before tool visibility patch checks");

  const auto decision = policy.evaluate(
      make_compose_result({
          "system: Visible tools=builtin.plan, mcp.read_only",
          "user: Analyze the latest audit signals",
      }),
      make_policy_input({"builtin:all", "mcp:trusted"}, {"builtin.plan", "mcp.read_only"},
                        512U));

  assert_equal(static_cast<int>(PromptPolicyDisposition::Allow),
               static_cast<int>(decision.disposition),
               "PromptPolicy should allow when visible tools match the configured visibility rules");
  assert_true(decision.governed_messages.size() == 2U,
              "PromptPolicy should preserve governed messages on the allow path");
  assert_true(contains_value(decision.tool_visibility_patch, "builtin:all") &&
                  contains_value(decision.tool_visibility_patch, "mcp:trusted"),
              "PromptPolicy should emit the matched tool visibility patch for auditability");
}

void test_prompt_policy_requires_recompose_when_visible_tool_has_no_rule() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptPolicy policy;
  assert_true(policy.init({.default_allowed_releases = {"stable"},
                           .default_trusted_sources = {"profiles"},
                           .deny_on_missing_allowlist = true}),
              "PromptPolicy should initialize before tool mismatch checks");

  const auto decision = policy.evaluate(
      make_compose_result({
          "system: Visible tools=builtin.plan, mcp.read_only",
          "user: Analyze the latest audit signals",
      }),
      make_policy_input({"builtin:all"}, {"builtin.plan", "mcp.read_only"}, 512U));

  assert_equal(static_cast<int>(PromptPolicyDisposition::RequireRecompose),
               static_cast<int>(decision.disposition),
               "PromptPolicy should request recomposition when the visible tool set drifts from Tool Policy Gate expectations");
  assert_true(decision.governed_messages.empty(),
              "PromptPolicy should not emit governed messages when recomposition is required");
  assert_true(contains_value(decision.tool_visibility_patch, "hide:mcp.read_only"),
              "PromptPolicy should surface the exact tool visibility patch needed for recomposition");
}

void test_prompt_policy_allows_after_redaction_reduces_render_budget_pressure() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptPolicy policy;
  assert_true(policy.init({.default_allowed_releases = {"stable"},
                           .default_trusted_sources = {"profiles"},
                           .deny_on_missing_allowlist = true}),
              "PromptPolicy should initialize before redaction checks");

  const auto decision = policy.evaluate(
      make_compose_result({
          "system: secret://planner/session/ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 token=ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
          "user: Summarize the diagnostics state",
      },
                          96),
      make_policy_input({"builtin:all"}, {"builtin.plan"}, 24U));

  assert_equal(static_cast<int>(PromptPolicyDisposition::Allow),
               static_cast<int>(decision.disposition),
               "PromptPolicy should re-evaluate budget after redaction and allow when the governed payload drops under the render budget");
  assert_true(!decision.redactions.empty(),
              "PromptPolicy should record the redaction operations it applied before allowing the prompt");
  assert_true(decision.governed_messages.size() == 2U &&
                  decision.governed_messages.front().find("[REDACTED_SECRET]") != std::string::npos,
              "PromptPolicy should emit redacted governed messages instead of the original sensitive payload");
}

void test_prompt_policy_returns_over_budget_when_redacted_payload_still_exceeds_limit() {
  using dasall::tests::support::assert_equal;

  PromptPolicy policy;
  policy.init({.default_allowed_releases = {"stable"},
               .default_trusted_sources = {"profiles"},
               .deny_on_missing_allowlist = true});

  const auto decision = policy.evaluate(
      make_compose_result({
          "system: This planner guidance remains intentionally long even after redaction because it includes a large amount of non-sensitive execution guidance that cannot be trimmed here.",
          "user: Produce a detailed summary of the current diagnostics state and remediation plan.",
      },
                          128,
                          std::vector<std::string>{"over_budget"}),
      make_policy_input({"builtin:all"}, {"builtin.plan"}, 8U));

  assert_equal(static_cast<int>(PromptPolicyDisposition::OverBudget),
               static_cast<int>(decision.disposition),
               "PromptPolicy should emit OverBudget when the governed payload still exceeds the configured render budget");
}

}  // namespace

int main() {
  try {
    test_prompt_policy_emits_tool_visibility_patch_for_matching_rules();
    test_prompt_policy_requires_recompose_when_visible_tool_has_no_rule();
    test_prompt_policy_allows_after_redaction_reduces_render_budget_pressure();
    test_prompt_policy_returns_over_budget_when_redacted_payload_still_exceeds_limit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}