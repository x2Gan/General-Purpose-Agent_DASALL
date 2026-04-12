#include "PromptPipeline.h"

#include <string>
#include <utility>
#include <vector>

namespace {

using ModelBudgetHint = dasall::llm::prompt::ModelBudgetHint;
using PromptComposeRequest = dasall::contracts::PromptComposeRequest;
using PromptComposeResult = dasall::contracts::PromptComposeResult;
using PromptPipelineResult = dasall::llm::prompt::PromptPipelineResult;
using PromptPolicyDecision = dasall::llm::prompt::PromptPolicyDecision;
using PromptPolicyDisposition = dasall::llm::prompt::PromptPolicyDisposition;
using PromptPolicyInput = dasall::llm::prompt::PromptPolicyInput;
using PromptQuery = dasall::llm::prompt::PromptQuery;
using PromptRegistryResult = dasall::llm::prompt::PromptRegistryResult;

ModelBudgetHint make_budget_hint(const PromptPolicyInput& input) {
  return ModelBudgetHint{
      .context_window = input.render_budget_tokens,
      .max_output_tokens = 0U,
      .reserved_output_tokens = 0U,
  };
}

bool compose_result_ready_for_policy(const PromptComposeResult& compose_result) {
  return compose_result.messages.has_value() && !compose_result.messages->empty() &&
         compose_result.selected_prompt_id.has_value() &&
         !compose_result.selected_prompt_id->empty() &&
         compose_result.selected_version.has_value() &&
         !compose_result.selected_version->empty() &&
         compose_result.estimated_tokens.has_value() && *compose_result.estimated_tokens >= 0;
}

std::string compose_failure_reason(const PromptComposeResult& compose_result) {
  if (compose_result.composition_warnings.has_value() &&
      !compose_result.composition_warnings->empty()) {
    return compose_result.composition_warnings->front();
  }

  if (!compose_result.messages.has_value() || compose_result.messages->empty()) {
    return "compose_messages_missing";
  }

  if (!compose_result.selected_prompt_id.has_value() ||
      compose_result.selected_prompt_id->empty() ||
      !compose_result.selected_version.has_value() ||
      compose_result.selected_version->empty()) {
    return "compose_selected_prompt_missing";
  }

  if (!compose_result.estimated_tokens.has_value() || *compose_result.estimated_tokens < 0) {
    return "compose_estimate_missing";
  }

  return "compose_failed";
}

PromptPolicyInput enrich_policy_input(const PromptPolicyInput& base_input,
                                      const PromptQuery& query,
                                      const PromptComposeRequest& compose_request,
                                      const PromptRegistryResult& registry_result) {
  PromptPolicyInput enriched = base_input;

  if (enriched.active_scene.empty() && !query.scene_id.empty()) {
    enriched.active_scene = query.scene_id;
  }

  if (enriched.active_persona.empty() && !query.persona_id.empty()) {
    enriched.active_persona = query.persona_id;
  }

  if (registry_result.release.has_value()) {
    if (registry_result.release->release_scope.has_value()) {
      enriched.selected_release_scope = *registry_result.release->release_scope;
    }

    if (registry_result.release->trusted_source.has_value()) {
      enriched.selected_trusted_source = *registry_result.release->trusted_source;
    }
  }

  if (enriched.selected_trusted_source.empty() && !registry_result.trusted_sources_matched.empty()) {
    enriched.selected_trusted_source = registry_result.trusted_sources_matched.front();
  }

  if (compose_request.visible_tools.has_value() && !compose_request.visible_tools->empty()) {
    enriched.visible_tools = *compose_request.visible_tools;
  } else if (!query.available_tools.empty()) {
    enriched.visible_tools = query.available_tools;
  }

  return enriched;
}

PromptPipelineResult make_registry_failure(PromptRegistryResult registry_result) {
  PromptPipelineResult result;
  result.disposition = PromptPolicyDisposition::Deny;
  result.registry_result = std::move(registry_result);

  if (result.registry_result.has_value()) {
    if (!result.registry_result->has_consistent_values()) {
      result.reason = "registry_result_inconsistent";
    } else {
      result.reason = result.registry_result->selection_reason;
    }
  }

  if (result.reason.empty()) {
    result.reason = "prompt_selection_failed";
  }

  return result;
}

PromptPipelineResult make_compose_failure(PromptRegistryResult registry_result,
                                          PromptComposeResult compose_result) {
  PromptPipelineResult result;
  result.disposition = PromptPolicyDisposition::Deny;
  result.registry_result = std::move(registry_result);
  result.compose_result = std::move(compose_result);
  result.reason = compose_failure_reason(*result.compose_result);
  return result;
}

PromptPipelineResult make_policy_result(PromptRegistryResult registry_result,
                                        PromptComposeResult compose_result,
                                        PromptPolicyDecision policy_decision) {
  PromptPipelineResult result;
  result.disposition = policy_decision.disposition;
  result.registry_result = std::move(registry_result);
  result.compose_result = std::move(compose_result);
  result.policy_decision = std::move(policy_decision);

  if (!result.policy_decision->has_consistent_values()) {
    result.disposition = PromptPolicyDisposition::Deny;
    result.reason = "policy_result_inconsistent";
    return result;
  }

  if (result.disposition != PromptPolicyDisposition::Allow) {
    result.reason = result.policy_decision->reason;
  }

  return result;
}

}  // namespace

namespace dasall::llm::prompt {

PromptPipeline::PromptPipeline(std::shared_ptr<IPromptRegistry> registry,
                               std::shared_ptr<IPromptComposer> composer,
                               std::shared_ptr<IPromptPolicy> policy)
    : registry_(std::move(registry)),
      composer_(std::move(composer)),
      policy_(std::move(policy)) {}

bool PromptPipeline::init(const PromptPipelineConfig& config) {
  initialized_ = false;

  if (registry_ == nullptr || composer_ == nullptr || policy_ == nullptr) {
    return false;
  }

  if (!registry_->init(config.registry_config) || !composer_->init(config.composer_config) ||
      !policy_->init(config.policy_config)) {
    return false;
  }

  initialized_ = true;
  return true;
}

PromptPipelineResult PromptPipeline::run(const PromptQuery& query,
                                         const PromptComposeRequest& compose_request,
                                         const PromptPolicyInput& policy_input) const {
  if (!initialized_) {
    return PromptPipelineResult{
        .disposition = PromptPolicyDisposition::Deny,
        .compose_result = std::nullopt,
        .policy_decision = std::nullopt,
        .registry_result = std::nullopt,
        .reason = "pipeline_not_initialized",
    };
  }

  PromptRegistryResult registry_result = registry_->select(query);
  if (!registry_result.has_consistent_values() || registry_result.code.has_value() ||
      !registry_result.release.has_value()) {
    return make_registry_failure(std::move(registry_result));
  }

  PromptComposeResult compose_result =
      composer_->compose(compose_request, *registry_result.release, make_budget_hint(policy_input));
  if (!compose_result_ready_for_policy(compose_result)) {
    return make_compose_failure(std::move(registry_result), std::move(compose_result));
  }

  PromptPolicyDecision policy_decision =
      policy_->evaluate(compose_result,
                        enrich_policy_input(policy_input, query, compose_request, registry_result));
  return make_policy_result(std::move(registry_result), std::move(compose_result),
                            std::move(policy_decision));
}

}  // namespace dasall::llm::prompt