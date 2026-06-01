#include "StagePolicyResolver.h"

#include <algorithm>
#include <string_view>

#include "RuntimePolicySnapshot.h"
#include "config/CognitionConfigProjector.h"

namespace dasall::cognition::policy {
namespace {

constexpr std::uint32_t kMinimumDeadlineMs = 300U;
constexpr std::uint32_t kDefaultReflectionRoundLimit = 2U;

[[nodiscard]] bool structured_local_fallback_allowed(std::string_view profile_id) {
  return profile_id == "edge_balanced" || profile_id == "edge_minimal" ||
         profile_id == "factory_test";
}

[[nodiscard]] bool append_stage_hint(StageExecutionPlan& plan,
                                     const profiles::RuntimePolicySnapshot& snapshot,
                                     std::string_view stage_name,
                                     std::string_view task_type) {
  const auto hint = config::CognitionConfigProjector::derive_stage_model_hint(
      snapshot, stage_name, task_type);
  if (!hint.has_value()) {
    return false;
  }

  plan.stage_model_hints.push_back(*hint);
  plan.preferred_model_tier = std::max(plan.preferred_model_tier, hint->capability_tier);
  return true;
}

[[nodiscard]] std::uint32_t derive_deadline_ms(std::int64_t llm_timeout_ms,
                                               const StageExecutionHints& hints) {
  const auto base_deadline = static_cast<std::uint32_t>(
      std::max<std::int64_t>(kMinimumDeadlineMs, llm_timeout_ms));
  if (!hints.low_latency_preferred) {
    return base_deadline;
  }

  return std::max(kMinimumDeadlineMs, base_deadline / 2U);
}

[[nodiscard]] float clamp_threshold(float value) {
  return std::clamp(value, 0.0F, 0.95F);
}

void apply_budget_constraints(StageExecutionPlan& plan,
                              const std::optional<BudgetContext>& budget_context) {
  if (!budget_context.has_value()) {
    return;
  }

  if (budget_context->budget_utilization >= 0.8F) {
    plan.max_plan_nodes = std::min<std::uint32_t>(plan.max_plan_nodes, 2U);
    plan.max_plan_depth = std::min<std::uint32_t>(plan.max_plan_depth, 2U);
    plan.reflection_round_limit = 1U;
    plan.clarification_threshold = clamp_threshold(plan.clarification_threshold + 0.20F);
    plan.degraded_mode_active = true;
    plan.fallback_mode = StageFallbackMode::Conservative;
  } else if (budget_context->budget_utilization >= 0.5F) {
    plan.max_plan_nodes = std::max<std::uint32_t>(1U, plan.max_plan_nodes / 2U);
    plan.clarification_threshold = clamp_threshold(plan.clarification_threshold + 0.10F);
    plan.degraded_mode_active = true;
    if (plan.fallback_mode == StageFallbackMode::None) {
      plan.fallback_mode = StageFallbackMode::Conservative;
    }
  }

  if (budget_context->context_was_truncated) {
    plan.clarification_threshold = clamp_threshold(plan.clarification_threshold + 0.05F);
  }

  if (budget_context->near_budget_limit && plan.fallback_mode == StageFallbackMode::None) {
    plan.fallback_mode = StageFallbackMode::Conservative;
    plan.degraded_mode_active = true;
  }
}

[[nodiscard]] bool has_non_empty_stages(const StageExecutionPlan& plan) {
  return !plan.enabled_stages.empty() && !plan.stage_model_hints.empty() &&
         plan.max_plan_nodes > 0U && plan.max_plan_depth > 0U && plan.deadline_ms > 0U;
}

}  // namespace

std::optional<StageExecutionPlan> StagePolicyResolver::resolve_decide_plan(
    const profiles::RuntimePolicySnapshot& snapshot,
    const CognitionStepRequest& request) {
  const auto config = config::CognitionConfigProjector::project_config(snapshot);
  if (!config.has_value()) {
    return std::nullopt;
  }

  StageExecutionPlan plan;
  plan.perception_llm_enabled = config->perception.llm_enabled;
  plan.enabled_stages = plan.perception_llm_enabled
                            ? std::vector<std::string>{"perception", "planning", "execution"}
                            : std::vector<std::string>{"planning", "execution"};
  plan.max_plan_nodes = config->max_plan_nodes;
  plan.max_plan_depth = config->max_plan_depth;
  plan.deadline_ms = derive_deadline_ms(snapshot.timeout_policy().llm.timeout_ms,
                                        request.execution_hints);
  plan.clarification_threshold = config->thresholds.ask_clarification;
  plan.rule_fallback_enabled =
      request.execution_hints.degraded_path_allowed && config->perception.rule_fallback_enabled &&
      structured_local_fallback_allowed(snapshot.effective_profile_id());
  plan.template_fallback_enabled = false;
  plan.reflection_round_limit = kDefaultReflectionRoundLimit;

  if ((plan.perception_llm_enabled &&
       !append_stage_hint(plan, snapshot, "perception", "perception")) ||
      !append_stage_hint(plan, snapshot, "planning", "plan") ||
      !append_stage_hint(plan, snapshot, "execution", "action_decision")) {
    return std::nullopt;
  }

  apply_budget_constraints(plan, request.budget_context);
  if (!has_non_empty_stages(plan)) {
    return std::nullopt;
  }

  return plan;
}

std::optional<StageExecutionPlan> StagePolicyResolver::resolve_reflection_plan(
    const profiles::RuntimePolicySnapshot& snapshot,
    const ReflectionRequest& request) {
  const auto config = config::CognitionConfigProjector::project_config(snapshot);
  if (!config.has_value()) {
    return std::nullopt;
  }

  StageExecutionPlan plan;
  plan.enabled_stages = {"reflection"};
  plan.max_plan_nodes = config->max_plan_nodes;
  plan.max_plan_depth = config->max_plan_depth;
  plan.deadline_ms = derive_deadline_ms(snapshot.timeout_policy().llm.timeout_ms,
                                        request.execution_hints);
  plan.clarification_threshold = config->thresholds.replan_hint;
  plan.rule_fallback_enabled = false;
  plan.template_fallback_enabled = false;
  plan.reflection_round_limit = kDefaultReflectionRoundLimit;

  if (!append_stage_hint(plan, snapshot, "reflection", "failure_analysis")) {
    return std::nullopt;
  }

  if (request.execution_hints.low_latency_preferred) {
    plan.fallback_mode = StageFallbackMode::Conservative;
    plan.degraded_mode_active = true;
  }

  if (!has_non_empty_stages(plan)) {
    return std::nullopt;
  }

  return plan;
}

std::optional<StageExecutionPlan> StagePolicyResolver::resolve_response_plan(
    const profiles::RuntimePolicySnapshot& snapshot,
    const ResponseBuildRequest& request) {
  const auto config = config::CognitionConfigProjector::project_config(snapshot);
  if (!config.has_value()) {
    return std::nullopt;
  }

  StageExecutionPlan plan;
  plan.enabled_stages = {"response"};
  plan.max_plan_nodes = config->max_plan_nodes;
  plan.max_plan_depth = config->max_plan_depth;
  plan.deadline_ms = static_cast<std::uint32_t>(
      std::max<std::int64_t>(kMinimumDeadlineMs, snapshot.timeout_policy().llm.timeout_ms));
  plan.clarification_threshold = config->thresholds.direct_response;
  plan.rule_fallback_enabled = false;
  plan.template_fallback_enabled =
      config->response.template_fallback_enabled && request.build_hints.allow_template_fallback;
  plan.reflection_round_limit = kDefaultReflectionRoundLimit;

  if (!append_stage_hint(plan, snapshot, "response", "final_response")) {
    return std::nullopt;
  }

  if (request.build_hints.prefer_template && plan.template_fallback_enabled) {
    plan.fallback_mode = StageFallbackMode::TemplatePreferred;
    plan.degraded_mode_active = true;
  } else if (snapshot.effective_profile_id() == "factory_test" &&
             plan.template_fallback_enabled) {
    plan.fallback_mode = StageFallbackMode::TemplatePreferred;
    plan.degraded_mode_active = true;
  } else if (plan.template_fallback_enabled) {
    plan.fallback_mode = StageFallbackMode::TemplateAllowed;
  }

  if (!has_non_empty_stages(plan)) {
    return std::nullopt;
  }

  return plan;
}

std::optional<StageModelHint> StagePolicyResolver::derive_stage_model_hint(
    const profiles::RuntimePolicySnapshot& snapshot,
    std::string_view stage_name,
    std::string_view task_type) {
  return config::CognitionConfigProjector::derive_stage_model_hint(
      snapshot, stage_name, task_type);
}

}  // namespace dasall::cognition::policy