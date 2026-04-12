#include <algorithm>
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

using dasall::contracts::CompositionStage;
using dasall::contracts::PromptComposeRequest;
using dasall::contracts::PromptComposeResult;
using dasall::contracts::PromptEvalStatus;
using dasall::contracts::PromptRelease;
using dasall::llm::prompt::FewShotResolver;
using dasall::llm::prompt::ModelBudgetHint;
using dasall::llm::prompt::PromptComposer;

PromptComposeRequest make_request() {
  return PromptComposeRequest{
      .request_id = "req-017-slot-mapping",
      .stage = CompositionStage::Planning,
      .context_packet_id = "ctx-slot-001",
      .created_at = 1712908800000,
      .task_type = "plan",
      .prompt_release_id = std::nullopt,
      .visible_tools = std::vector<std::string>{"builtin.plan", "mcp.read_only"},
      .model_route = "cloud.reasoning",
      .output_schema_ref = "schema://planner/default",
      .response_format = std::nullopt,
      .tags = std::vector<std::string>{"planner", "diagnostics"},
  };
}

PromptRelease make_release() {
  return PromptRelease{
      .prompt_id = "planner",
      .version = "2026.04.12",
      .stage = CompositionStage::Planning,
      .eval_status = PromptEvalStatus::Stable,
      .release_scope = "stable",
      .system_instructions =
          "Stage={{stage}} task={{task_type}} tools={{visible_tools}}",
      .task_template =
          "Context={{context_packet_id}} route={{model_route}} schema={{output_schema_ref}}",
      .output_schema_ref = "schema://planner/default",
      .few_shot_refs = std::vector<std::string>{"shot-1", "shot-2", "shot-3"},
        .policy_notes = std::nullopt,
        .rollback_from = std::nullopt,
      .trusted_source = "profiles",
      .tags = std::vector<std::string>{"planner"},
  };
}

bool has_warning(const PromptComposeResult& result, const std::string& warning) {
  return result.composition_warnings.has_value() &&
         std::find(result.composition_warnings->begin(), result.composition_warnings->end(), warning) !=
             result.composition_warnings->end();
}

void test_prompt_composer_maps_request_fields_and_injects_capped_few_shots() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const FewShotResolver few_shot_resolver =
      [](const PromptRelease&, std::uint32_t max_few_shot_count, std::vector<std::string>& warnings) {
        std::vector<std::string> resolved = {
            "assistant: example-1",
            "user: example-2",
            "assistant: example-3",
        };

        if (resolved.size() > max_few_shot_count) {
          resolved.resize(max_few_shot_count);
          warnings.push_back("few_shot_count_capped");
        }

        return resolved;
      };

  PromptComposer composer(std::make_shared<dasall::llm::prompt::TemplateRenderer>(),
                          std::make_shared<dasall::llm::TokenEstimator>(),
                          few_shot_resolver);
  assert_true(composer.init({.template_engine = "simple_var", .max_few_shot_count = 2U}),
              "PromptComposer should initialize before slot mapping verification");

  const auto result = composer.compose(
      make_request(), make_release(), ModelBudgetHint{.context_window = 512U, .max_output_tokens = 64U});

  assert_true(result.messages.has_value(),
              "PromptComposer should emit provider-neutral messages after rendering the selected release");
  assert_equal(4, static_cast<int>(result.messages->size()),
               "PromptComposer should emit system + capped few-shot + user messages in order");
  assert_equal(std::string("system: Stage=planning task=plan tools=builtin.plan, mcp.read_only"),
               result.messages->at(0),
               "PromptComposer should map stage, task_type and visible_tools into the system template");
  assert_equal(std::string("assistant: example-1"), result.messages->at(1),
               "PromptComposer should inject the first resolved few-shot example after the system message");
  assert_equal(std::string("user: example-2"), result.messages->at(2),
               "PromptComposer should keep resolved few-shots in resolver order");
  assert_equal(std::string("user: Context=ctx-slot-001 route=cloud.reasoning schema=schema://planner/default"),
               result.messages->at(3),
               "PromptComposer should map context_packet_id, model_route and output_schema_ref into the task template");
  assert_true(result.estimated_tokens.has_value() && *result.estimated_tokens > 0,
              "PromptComposer should publish estimated token usage for PromptPolicy handoff");
  assert_equal(std::string("planner"), *result.selected_prompt_id,
               "PromptComposer should preserve the selected prompt identity from PromptRelease");
  assert_equal(std::string("2026.04.12"), *result.selected_version,
               "PromptComposer should preserve the selected PromptRelease version for auditability");
  assert_true(has_warning(result, "few_shot_count_capped"),
              "PromptComposer should surface few-shot capping as a composition warning instead of silently dropping examples");
}

void test_prompt_composer_keeps_unmatched_slots_as_literal_warnings() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptRelease release = make_release();
  release.task_template = "Goal={{user_goal}} / Context={{context_packet_id}}";
  release.few_shot_refs = std::nullopt;

  PromptComposer composer;
  assert_true(composer.init({.template_engine = "simple_var", .max_few_shot_count = 2U}),
              "PromptComposer should initialize before unmatched slot verification");

  const auto result = composer.compose(
      make_request(), release, ModelBudgetHint{.context_window = 512U, .max_output_tokens = 64U});

  assert_true(result.messages.has_value(),
              "PromptComposer should still emit messages when a template slot remains unmatched");
  assert_equal(std::string("user: Goal={{user_goal}} / Context=ctx-slot-001"),
               result.messages->back(),
               "PromptComposer should preserve unmatched placeholders as literal text instead of fabricating semantic input");
  assert_true(has_warning(result, "unmatched_variable:user_goal"),
              "PromptComposer should surface unmatched slots as composition warnings for PromptPolicy and audit consumers");
}

}  // namespace

int main() {
  try {
    test_prompt_composer_maps_request_fields_and_injects_capped_few_shots();
    test_prompt_composer_keeps_unmatched_slots_as_literal_warnings();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}