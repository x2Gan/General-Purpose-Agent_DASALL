#include "config/CognitionConfigProjector.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "RuntimePolicySnapshot.h"

namespace dasall::cognition::config {
namespace {

constexpr std::array<std::string_view, 5> kCanonicalStageNames = {
  "perception",
    "planning",
    "execution",
    "reflection",
    "response",
};

[[nodiscard]] bool is_known_profile(std::string_view profile_id) {
  return profile_id == "desktop_full" || profile_id == "cloud_full" ||
         profile_id == "edge_balanced" || profile_id == "edge_minimal" ||
         profile_id == "factory_test";
}

[[nodiscard]] bool is_canonical_stage_name(std::string_view stage_name) {
  return std::find(kCanonicalStageNames.begin(), kCanonicalStageNames.end(), stage_name) !=
         kCanonicalStageNames.end();
}

[[nodiscard]] bool has_required_stage_routes(const profiles::RuntimePolicySnapshot& snapshot) {
  const auto& stage_routes = snapshot.model_profile().stage_routes;
  return std::all_of(
      kCanonicalStageNames.begin(), kCanonicalStageNames.end(), [&](std::string_view stage_name) {
        const auto it = stage_routes.find(std::string(stage_name));
        return it != stage_routes.end() && it->second.has_consistent_values();
      });
}

[[nodiscard]] bool degrade_allowed(const profiles::RuntimePolicySnapshot& snapshot) {
  return snapshot.degrade_policy().allow_budget_degrade ||
         !snapshot.degrade_policy().fallback_chain.empty();
}

void apply_reasoner_candidate_weights(std::string_view profile_id,
                                      CognitionConfig& config) {
  if (profile_id == "desktop_full" || profile_id == "cloud_full") {
    config.reasoner.candidate_weights.tool_call = 1.05F;
    config.reasoner.candidate_weights.direct_response = 0.95F;
    config.reasoner.candidate_weights.clarification = 1.00F;
    config.reasoner.candidate_weights.converge_safe = 0.95F;
    return;
  }

  if (profile_id == "edge_balanced") {
    config.reasoner.candidate_weights.tool_call = 0.95F;
    config.reasoner.candidate_weights.direct_response = 1.05F;
    config.reasoner.candidate_weights.clarification = 1.05F;
    config.reasoner.candidate_weights.converge_safe = 1.05F;
    return;
  }

  if (profile_id == "edge_minimal") {
    config.reasoner.candidate_weights.tool_call = 0.90F;
    config.reasoner.candidate_weights.direct_response = 1.10F;
    config.reasoner.candidate_weights.clarification = 1.05F;
    config.reasoner.candidate_weights.converge_safe = 1.10F;
    return;
  }

  if (profile_id == "factory_test") {
    config.reasoner.candidate_weights.tool_call = 0.95F;
    config.reasoner.candidate_weights.direct_response = 1.00F;
    config.reasoner.candidate_weights.clarification = 1.10F;
    config.reasoner.candidate_weights.converge_safe = 1.10F;
  }
}

void apply_response_templates(std::string_view profile_id,
                              CognitionConfig& config) {
  if (profile_id == "desktop_full" || profile_id == "cloud_full") {
    return;
  }

  if (profile_id == "edge_balanced") {
    config.response.templates.clarification =
        "I need a bit more detail before I continue. Current summary: {summary}";
    config.response.templates.safe_converge =
        "I am returning a compact safe response for this environment. {summary}";
    config.response.templates.fallback_failure =
        "I could not validate a final response on this route. Current summary: {summary}";
    return;
  }

  if (profile_id == "edge_minimal") {
    config.response.templates.clarification =
        "Need clarification before continuing: {summary}";
    config.response.templates.safe_converge =
        "Returning a safe fallback response: {summary}";
    config.response.templates.fallback_failure =
        "Validated final response unavailable: {summary}";
    return;
  }

  if (profile_id == "factory_test") {
    config.response.templates.clarification =
        "Clarification required before the workflow can continue. Summary seed: {summary}";
    config.response.templates.safe_converge =
        "Diagnostic safe-converge response emitted. Summary seed: {summary}";
    config.response.templates.fallback_failure =
        "Diagnostic fallback failure response emitted. Summary seed: {summary}";
  }
}

[[nodiscard]] CognitionConfig merge_profile_defaults(
    std::string_view profile_id,
    const profiles::RuntimePolicySnapshot& snapshot) {
  CognitionConfig config;
  config.enabled = true;
  config.perception.llm_enabled = profile_id != "edge_minimal";
  config.perception.rule_fallback_enabled = degrade_allowed(snapshot);
  config.response.template_fallback_enabled = degrade_allowed(snapshot);
  config.reasoner.allow_delegate_hint = false;
  config.observability.emit_stage_spans =
      snapshot.ops_policy().trace_sample_ratio > 0.0 || profile_id == "factory_test";
  config.observability.redact_context_payload = true;

  if (profile_id == "edge_balanced") {
    config.max_plan_nodes = 6U;
    config.max_plan_depth = 3U;
  } else if (profile_id == "edge_minimal") {
    config.max_plan_nodes = 4U;
    config.max_plan_depth = 2U;
  } else if (profile_id == "factory_test") {
    config.max_plan_nodes = 6U;
    config.max_plan_depth = 4U;
  }

  apply_reasoner_candidate_weights(profile_id, config);
  apply_response_templates(profile_id, config);

  return config;
}

[[nodiscard]] ModelCapabilityTier derive_capability_tier(std::string_view profile_id,
                                                         std::string_view stage_name,
                                                         std::string_view task_type) {
  if (stage_name == "perception") {
    return profile_id == "edge_minimal" ? ModelCapabilityTier::Lightweight
                                         : ModelCapabilityTier::Standard;
  }

  if (stage_name == "planning") {
    if (task_type == "replan") {
      return (profile_id == "desktop_full" || profile_id == "cloud_full")
                 ? ModelCapabilityTier::Advanced
                 : ModelCapabilityTier::Standard;
    }

    return (profile_id == "desktop_full" || profile_id == "cloud_full")
               ? ModelCapabilityTier::Advanced
               : ModelCapabilityTier::Standard;
  }

  if (stage_name == "execution") {
    return profile_id == "edge_minimal" ? ModelCapabilityTier::Lightweight
                                         : ModelCapabilityTier::Standard;
  }

  if (stage_name == "reflection") {
    return (profile_id == "desktop_full" || profile_id == "cloud_full")
               ? ModelCapabilityTier::ReasoningHeavy
               : ModelCapabilityTier::Advanced;
  }

  return profile_id == "edge_minimal" ? ModelCapabilityTier::Lightweight
                                       : ModelCapabilityTier::Standard;
}

[[nodiscard]] bool requires_structured_output(std::string_view stage_name) {
  return stage_name != "response";
}

[[nodiscard]] bool requires_reasoning_trace(std::string_view stage_name,
                                            std::string_view,
                                            ModelCapabilityTier capability_tier) {
  if (stage_name == "reflection") {
    return true;
  }

  if (stage_name == "planning") {
    return capability_tier >= ModelCapabilityTier::Advanced;
  }

  return false;
}

[[nodiscard]] float derive_cost_sensitivity(std::string_view profile_id) {
  if (profile_id == "edge_minimal") {
    return 0.90F;
  }

  if (profile_id == "edge_balanced") {
    return 0.70F;
  }

  if (profile_id == "factory_test") {
    return 0.55F;
  }

  if (profile_id == "cloud_full") {
    return 0.25F;
  }

  return 0.35F;
}

}  // namespace

std::optional<CognitionConfig> CognitionConfigProjector::project_config(
    const profiles::RuntimePolicySnapshot& snapshot) {
  if (!snapshot.has_consistent_values() || !is_known_profile(snapshot.effective_profile_id()) ||
      !has_required_stage_routes(snapshot)) {
    return std::nullopt;
  }

  return merge_profile_defaults(snapshot.effective_profile_id(), snapshot);
}

std::optional<StageModelHint> CognitionConfigProjector::derive_stage_model_hint(
    const profiles::RuntimePolicySnapshot& snapshot,
    std::string_view stage_name,
    std::string_view task_type) {
  if (!snapshot.has_consistent_values() || !is_known_profile(snapshot.effective_profile_id()) ||
      !is_canonical_stage_name(stage_name) || task_type.empty() || !has_required_stage_routes(snapshot)) {
    return std::nullopt;
  }

  const auto route_it = snapshot.model_profile().stage_routes.find(std::string(stage_name));
  if (route_it == snapshot.model_profile().stage_routes.end() ||
      !route_it->second.has_consistent_values()) {
    return std::nullopt;
  }

  StageModelHint hint;
  hint.stage_name = std::string(stage_name);
  hint.task_type = std::string(task_type);
  hint.capability_tier =
      derive_capability_tier(snapshot.effective_profile_id(), stage_name, task_type);
  hint.max_output_tokens = snapshot.token_budget_policy().max_output_tokens;
  hint.deadline_ms = static_cast<std::uint32_t>(snapshot.timeout_policy().llm.timeout_ms);
  hint.requires_structured_output = requires_structured_output(stage_name);
  hint.requires_reasoning_trace =
      requires_reasoning_trace(stage_name, task_type, hint.capability_tier);
  hint.cost_sensitivity = derive_cost_sensitivity(snapshot.effective_profile_id());
  hint.preferred_provider = route_it->second.route;
  return hint;
}

}  // namespace dasall::cognition::config