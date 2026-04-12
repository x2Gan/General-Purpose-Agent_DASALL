#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "error/ResultCode.h"
#include "prompt/ModelBudgetHint.h"
#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptComposeResult.h"
#include "prompt/PromptPolicyDecision.h"
#include "prompt/PromptPolicyInput.h"
#include "prompt/PromptQuery.h"
#include "prompt/PromptRegistryResult.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/PromptPipeline.h"

namespace {

using dasall::contracts::CompositionStage;
using dasall::contracts::PromptComposeRequest;
using dasall::contracts::PromptComposeResult;
using dasall::contracts::PromptEvalStatus;
using dasall::contracts::PromptRelease;
using dasall::contracts::ResultCode;
using dasall::llm::prompt::IPromptComposer;
using dasall::llm::prompt::IPromptPolicy;
using dasall::llm::prompt::IPromptRegistry;
using dasall::llm::prompt::ModelBudgetHint;
using dasall::llm::prompt::PromptComposerConfig;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::prompt::PromptPipelineConfig;
using dasall::llm::prompt::PromptPolicyConfig;
using dasall::llm::prompt::PromptPolicyDecision;
using dasall::llm::prompt::PromptPolicyDisposition;
using dasall::llm::prompt::PromptPolicyInput;
using dasall::llm::prompt::PromptQuery;
using dasall::llm::prompt::PromptRegistryConfig;
using dasall::llm::prompt::PromptRegistryResult;

class RecordingRegistry final : public IPromptRegistry {
 public:
  bool init(const PromptRegistryConfig&) override {
    initialized = init_result;
    return init_result;
  }

  PromptRegistryResult select(const PromptQuery& query) const override {
    ++select_calls;
    last_query = query;
    return select_result;
  }

  bool init_result = true;
  bool initialized = false;
  PromptRegistryResult select_result;
  mutable std::size_t select_calls = 0U;
  mutable PromptQuery last_query;
};

class RecordingComposer final : public IPromptComposer {
 public:
  bool init(const PromptComposerConfig&) override {
    initialized = init_result;
    return init_result;
  }

  PromptComposeResult compose(const PromptComposeRequest& request,
                              const PromptRelease& release,
                              const ModelBudgetHint& budget_hint) const override {
    ++compose_calls;
    last_request = request;
    last_release = release;
    last_budget_hint = budget_hint;
    return compose_result;
  }

  bool init_result = true;
  bool initialized = false;
  PromptComposeResult compose_result;
  mutable std::size_t compose_calls = 0U;
  mutable std::optional<PromptComposeRequest> last_request;
  mutable std::optional<PromptRelease> last_release;
  mutable std::optional<ModelBudgetHint> last_budget_hint;
};

class RecordingPolicy final : public IPromptPolicy {
 public:
  bool init(const PromptPolicyConfig&) override {
    initialized = init_result;
    return init_result;
  }

  PromptPolicyDecision evaluate(const PromptComposeResult& compose_result,
                                const PromptPolicyInput& input) const override {
    ++evaluate_calls;
    last_compose_result = compose_result;
    last_input = input;
    return decision;
  }

  bool init_result = true;
  bool initialized = false;
  PromptPolicyDecision decision;
  mutable std::size_t evaluate_calls = 0U;
  mutable std::optional<PromptComposeResult> last_compose_result;
  mutable std::optional<PromptPolicyInput> last_input;
};

PromptRelease make_release() {
  return PromptRelease{
      .prompt_id = "planner",
      .version = "2026.04.12",
      .stage = CompositionStage::Planning,
      .eval_status = PromptEvalStatus::Stable,
      .release_scope = "stable",
      .system_instructions = "system guidance",
      .task_template = "task guidance",
      .output_schema_ref = std::nullopt,
      .few_shot_refs = std::nullopt,
      .policy_notes = std::nullopt,
      .rollback_from = std::nullopt,
      .trusted_source = "profiles",
      .tags = std::nullopt,
  };
}

PromptRegistryResult make_registry_success() {
  return PromptRegistryResult{
      .code = std::nullopt,
      .release = make_release(),
      .selected_prompt_id = "planner",
      .selected_version = "2026.04.12",
      .selection_reason = "default_release",
      .trusted_sources_matched = {"profiles"},
  };
}

PromptComposeResult make_compose_success() {
  return PromptComposeResult{
      .messages = std::vector<std::string>{
          "system: system guidance",
          "user: task guidance",
      },
      .selected_prompt_id = "planner",
      .selected_version = "2026.04.12",
      .estimated_tokens = 32,
      .pruned_sections = std::nullopt,
      .composition_warnings = std::nullopt,
  };
}

PromptQuery make_query() {
  return PromptQuery{
      .stage = "planning",
      .task_type = "diagnose",
      .language = "zh-CN",
      .model_family = "deepseek",
      .prompt_release_id = std::string(),
      .scene_id = "ops_diagnosis",
      .persona_id = "default_planner",
      .profile_id = "desktop_full",
      .available_tools = {"builtin.plan"},
      .trusted_sources = {"profiles"},
  };
}

PromptComposeRequest make_compose_request() {
  return PromptComposeRequest{
      .request_id = "req-019",
      .stage = CompositionStage::Planning,
      .context_packet_id = "ctx-019",
      .created_at = 1712870400000LL,
      .task_type = "diagnose",
      .prompt_release_id = std::nullopt,
      .visible_tools = std::vector<std::string>{"builtin.plan"},
      .model_route = "cloud.reasoning",
      .output_schema_ref = "schema://planner",
      .response_format = "json_object",
      .tags = std::vector<std::string>{"unit", "pipeline"},
  };
}

PromptPolicyInput make_policy_input() {
  return PromptPolicyInput{
      .profile_id = "desktop_full",
      .allowed_prompt_releases = {"stable"},
      .trusted_sources = {"profiles"},
      .tool_visibility_rules = {"builtin:all"},
      .render_budget_tokens = 256U,
      .active_scene = std::string(),
      .active_persona = std::string(),
      .selected_release_scope = std::string(),
      .selected_trusted_source = std::string(),
      .visible_tools = {},
  };
}

PromptPipelineConfig make_pipeline_config() {
  return PromptPipelineConfig{
      .registry_config = PromptRegistryConfig{},
      .composer_config = PromptComposerConfig{
          .template_engine = "simple_var",
          .max_few_shot_count = 5U,
      },
      .policy_config = PromptPolicyConfig{
          .default_allowed_releases = {"stable"},
          .default_trusted_sources = {"profiles"},
          .deny_on_missing_allowlist = true,
      },
  };
}

void test_prompt_pipeline_stops_after_select_failure() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto registry = std::make_shared<RecordingRegistry>();
  const auto composer = std::make_shared<RecordingComposer>();
  const auto policy = std::make_shared<RecordingPolicy>();

  registry->select_result = PromptRegistryResult{
      .code = ResultCode::ValidationFieldMissing,
      .release = std::nullopt,
      .selected_prompt_id = std::string(),
      .selected_version = std::string(),
      .selection_reason = "no_matching_prompt_release",
      .trusted_sources_matched = {},
  };

  PromptPipeline pipeline(registry, composer, policy);
  assert_true(pipeline.init(make_pipeline_config()),
              "PromptPipeline should initialize when injected collaborators initialize successfully");

  const auto result = pipeline.run(make_query(), make_compose_request(), make_policy_input());

  assert_equal(static_cast<int>(PromptPolicyDisposition::Deny),
               static_cast<int>(result.disposition),
               "PromptPipeline should fail closed when PromptRegistry cannot select a release");
  assert_true(result.registry_result.has_value() && !result.compose_result.has_value() &&
                  !result.policy_decision.has_value(),
              "PromptPipeline should stop before compose/evaluate when select fails");
  assert_true(result.reason == "no_matching_prompt_release",
              "PromptPipeline should surface PromptRegistry failure reason verbatim");
  assert_true(composer->compose_calls == 0U && policy->evaluate_calls == 0U,
              "PromptPipeline should not call downstream owners after select failure");
}

void test_prompt_pipeline_propagates_over_budget_and_enriches_policy_input() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto registry = std::make_shared<RecordingRegistry>();
  const auto composer = std::make_shared<RecordingComposer>();
  const auto policy = std::make_shared<RecordingPolicy>();

  registry->select_result = make_registry_success();
  composer->compose_result = make_compose_success();
  policy->decision = PromptPolicyDecision{
      .disposition = PromptPolicyDisposition::OverBudget,
      .governed_messages = {},
      .redactions = {"secret_ref"},
      .tool_visibility_patch = {"builtin:all"},
      .reason = "render_budget_exceeded",
  };

  PromptPipeline pipeline(registry, composer, policy);
  assert_true(pipeline.init(make_pipeline_config()),
              "PromptPipeline should initialize before over-budget propagation checks");

  const auto query = make_query();
  const auto compose_request = make_compose_request();
  const auto policy_input = make_policy_input();
  const auto result = pipeline.run(query, compose_request, policy_input);

  assert_equal(static_cast<int>(PromptPolicyDisposition::OverBudget),
               static_cast<int>(result.disposition),
               "PromptPipeline should propagate OverBudget from PromptPolicy without secondary trimming");
  assert_true(result.registry_result.has_value() && result.compose_result.has_value() &&
                  result.policy_decision.has_value(),
              "PromptPipeline should preserve registry, compose, and policy outputs on the over-budget path");
  assert_true(result.reason == "render_budget_exceeded",
              "PromptPipeline should surface PromptPolicy over-budget reason for Runtime fallback");
  assert_true(composer->last_budget_hint.has_value() &&
                  composer->last_budget_hint->context_window == 256U,
              "PromptPipeline should bridge render_budget_tokens into PromptComposer via ModelBudgetHint");
  assert_true(policy->last_input.has_value() &&
                  policy->last_input->selected_release_scope == "stable" &&
                  policy->last_input->selected_trusted_source == "profiles" &&
                  policy->last_input->visible_tools.size() == 1U,
              "PromptPipeline should enrich PromptPolicyInput with selected release/source metadata and visible tools");
}

void test_prompt_pipeline_returns_policy_deny_after_successful_compose() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto registry = std::make_shared<RecordingRegistry>();
  const auto composer = std::make_shared<RecordingComposer>();
  const auto policy = std::make_shared<RecordingPolicy>();

  registry->select_result = make_registry_success();
  composer->compose_result = make_compose_success();
  policy->decision = PromptPolicyDecision{
      .disposition = PromptPolicyDisposition::Deny,
      .governed_messages = {},
      .redactions = {},
      .tool_visibility_patch = {},
      .reason = "prompt_release_not_allowed",
  };

  PromptPipeline pipeline(registry, composer, policy);
  assert_true(pipeline.init(make_pipeline_config()),
              "PromptPipeline should initialize before deny-path checks");

  const auto result = pipeline.run(make_query(), make_compose_request(), make_policy_input());

  assert_equal(static_cast<int>(PromptPolicyDisposition::Deny),
               static_cast<int>(result.disposition),
               "PromptPipeline should propagate deny decisions from PromptPolicy");
  assert_true(result.compose_result.has_value() && result.policy_decision.has_value(),
              "PromptPipeline should keep compose and policy artifacts on deny");
  assert_true(result.reason == "prompt_release_not_allowed",
              "PromptPipeline should preserve the PromptPolicy deny reason");
}

void test_prompt_pipeline_returns_allow_with_full_artifacts() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto registry = std::make_shared<RecordingRegistry>();
  const auto composer = std::make_shared<RecordingComposer>();
  const auto policy = std::make_shared<RecordingPolicy>();

  registry->select_result = make_registry_success();
  composer->compose_result = make_compose_success();
  policy->decision = PromptPolicyDecision{
      .disposition = PromptPolicyDisposition::Allow,
      .governed_messages = {"system: governed", "user: governed"},
      .redactions = {},
      .tool_visibility_patch = {"builtin:all"},
      .reason = "allow",
  };

  PromptPipeline pipeline(registry, composer, policy);
  assert_true(pipeline.init(make_pipeline_config()),
              "PromptPipeline should initialize before allow-path checks");

  const auto result = pipeline.run(make_query(), make_compose_request(), make_policy_input());

  assert_equal(static_cast<int>(PromptPolicyDisposition::Allow),
               static_cast<int>(result.disposition),
               "PromptPipeline should return Allow when all three prompt-governance stages succeed");
  assert_true(result.registry_result.has_value() && result.compose_result.has_value() &&
                  result.policy_decision.has_value(),
              "PromptPipeline should surface all intermediate artifacts on the allow path");
  assert_true(result.reason.empty(),
              "PromptPipeline should keep the top-level reason empty on the allow path");
}

}  // namespace

int main() {
  try {
    test_prompt_pipeline_stops_after_select_failure();
    test_prompt_pipeline_propagates_over_budget_and_enriches_policy_input();
    test_prompt_pipeline_returns_policy_deny_after_successful_compose();
    test_prompt_pipeline_returns_allow_with_full_artifacts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}