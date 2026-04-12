#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "prompt/ModelBudgetHint.h"
#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptRelease.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/PromptComposer.h"

namespace {

using dasall::contracts::PromptComposeResult;
using dasall::llm::prompt::FewShotResolver;

dasall::contracts::PromptComposeRequest make_request() {
  return dasall::contracts::PromptComposeRequest{
      .request_id = "req-017-budget",
      .stage = dasall::contracts::CompositionStage::Planning,
      .context_packet_id = "ctx-017-budget",
      .created_at = 1712908800000,
      .task_type = "plan",
      .prompt_release_id = std::nullopt,
      .visible_tools = std::vector<std::string>{"builtin.plan"},
      .model_route = "cloud.reasoning",
      .output_schema_ref = "schema://planner/default",
      .response_format = std::nullopt,
      .tags = std::nullopt,
  };
}

dasall::contracts::PromptRelease make_release(std::string system_text,
                                              std::string task_text) {
  return dasall::contracts::PromptRelease{
      .prompt_id = "planner",
      .version = "2026.04.12",
      .stage = dasall::contracts::CompositionStage::Planning,
      .eval_status = dasall::contracts::PromptEvalStatus::Stable,
      .release_scope = "stable",
      .system_instructions = std::move(system_text),
      .task_template = std::move(task_text),
      .output_schema_ref = "schema://planner/default",
      .few_shot_refs = std::nullopt,
      .policy_notes = std::nullopt,
      .rollback_from = std::nullopt,
      .trusted_source = "profiles",
      .tags = std::vector<std::string>{"planner"},
  };
}

bool has_warning(const dasall::contracts::PromptComposeResult& result, const std::string& warning) {
  return result.composition_warnings.has_value() &&
         std::find(result.composition_warnings->begin(), result.composition_warnings->end(), warning) !=
             result.composition_warnings->end();
}

void assert_no_budget_prune(const PromptComposeResult& result) {
  using dasall::tests::support::assert_true;

  assert_true(!result.pruned_sections.has_value(),
              "PromptComposer should not prune sections on its own when budget is exceeded");
}

void test_prompt_composer_marks_budget_overflow_without_reordering_messages() {
  using dasall::llm::prompt::PromptComposer;
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptComposer composer;
  assert_true(composer.init({.template_engine = "simple_var", .max_few_shot_count = 2U}),
              "PromptComposer should initialize with the default simple_var renderer");

  const ModelBudgetHint budget_hint{
      .context_window = 24U,
      .max_output_tokens = 8U,
      .reserved_output_tokens = 8U,
  };

  const auto result = composer.compose(
      make_request(),
      make_release(std::string(80U, 'a'), std::string(80U, 'b')),
      budget_hint);

  assert_true(result.messages.has_value(),
              "PromptComposer should still emit provider-neutral messages when it only needs to warn about budget pressure");
  assert_equal(2, static_cast<int>(result.messages->size()),
               "PromptComposer should not silently drop system or user messages when no few-shot trimming is available");
  assert_true(result.estimated_tokens.has_value() && *result.estimated_tokens > 0,
              "PromptComposer should always publish estimated_tokens for PromptPolicy handoff");
  assert_true(has_warning(result, "over_budget"),
              "PromptComposer should emit an explicit over_budget warning when the composed payload exceeds the hint window");
  assert_no_budget_prune(result);
}

void test_prompt_composer_stays_within_budget_for_short_prompt() {
  using dasall::llm::prompt::PromptComposer;
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::tests::support::assert_true;

  PromptComposer composer;
  assert_true(composer.init({.template_engine = "simple_var", .max_few_shot_count = 2U}),
              "PromptComposer should initialize before evaluating a short prompt");

  const ModelBudgetHint budget_hint{
      .context_window = 256U,
      .max_output_tokens = 64U,
      .reserved_output_tokens = 32U,
  };

  const auto result = composer.compose(
      make_request(),
      make_release("system: concise plan", "summarize diagnostics for {{context_packet_id}}"),
      budget_hint);

  assert_true(result.messages.has_value(),
              "PromptComposer should emit messages for a short prompt under a generous budget");
  assert_true(!has_warning(result, "over_budget"),
              "PromptComposer should avoid over_budget warnings for short prompts under a generous window");
}

void test_prompt_composer_keeps_few_shots_when_budget_is_exceeded() {
  using dasall::llm::prompt::ModelBudgetHint;
  using dasall::llm::prompt::PromptComposer;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const FewShotResolver few_shot_resolver =
      [](const dasall::contracts::PromptRelease&, std::uint32_t, std::vector<std::string>&) {
        return std::vector<std::string>{
            "assistant: example-1",
            "user: example-2",
        };
      };

  PromptComposer composer(std::make_shared<dasall::llm::prompt::TemplateRenderer>(),
                          std::make_shared<dasall::llm::TokenEstimator>(),
                          few_shot_resolver);
  assert_true(composer.init({.template_engine = "simple_var", .max_few_shot_count = 2U}),
              "PromptComposer should initialize before budget overflow retention checks");

  auto release = make_release(std::string(80U, 'a'), std::string(80U, 'b'));
  release.few_shot_refs = std::vector<std::string>{"shot-1", "shot-2"};

  const auto result = composer.compose(
      make_request(),
      release,
      ModelBudgetHint{
          .context_window = 24U,
          .max_output_tokens = 8U,
          .reserved_output_tokens = 8U,
      });

  assert_true(result.messages.has_value(),
              "PromptComposer should keep the rendered payload even when it only has an over_budget warning to report");
  assert_equal(4, static_cast<int>(result.messages->size()),
               "PromptComposer should keep system, few-shot, and user messages intact under over_budget conditions");
  assert_true(has_warning(result, "over_budget"),
              "PromptComposer should still emit over_budget after retaining the full rendered payload");
  assert_no_budget_prune(result);
}

}  // namespace

int main() {
  try {
    test_prompt_composer_marks_budget_overflow_without_reordering_messages();
    test_prompt_composer_stays_within_budget_for_short_prompt();
    test_prompt_composer_keeps_few_shots_when_budget_is_exceeded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}