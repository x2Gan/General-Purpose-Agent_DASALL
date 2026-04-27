#include "perception/PerceptionEngine.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "validation/InputBoundaryValidator.h"

namespace dasall::cognition::perception {
namespace {

[[nodiscard]] std::string to_lower(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

[[nodiscard]] bool contains_any(std::string_view haystack,
                                const std::array<std::string_view, 5>& needles) {
  const auto lowered_haystack = to_lower(haystack);
  return std::any_of(needles.begin(), needles.end(), [&](std::string_view needle) {
    return lowered_haystack.find(std::string(needle)) != std::string::npos;
  });
}

[[nodiscard]] std::string summarize_intent(const CognitionStepRequest& request) {
  if (request.context_packet.current_goal_summary.has_value() &&
      !request.context_packet.current_goal_summary->empty()) {
    return *request.context_packet.current_goal_summary;
  }

  return *request.goal_contract.goal_description;
}

[[nodiscard]] std::string derive_task_type(const CognitionStepRequest& request) {
  const auto combined_text = to_lower(*request.context_packet.user_turn + " " +
                                      *request.goal_contract.goal_description);
  if (combined_text.find("plan") != std::string::npos ||
      combined_text.find("steps") != std::string::npos) {
    return "plan";
  }

  if (combined_text.find("summarize") != std::string::npos ||
      combined_text.find("reply") != std::string::npos ||
      combined_text.find("respond") != std::string::npos) {
    return "final_response";
  }

  return "action_decision";
}

[[nodiscard]] std::vector<EntityCandidate> extract_entities(const CognitionStepRequest& request) {
  std::vector<EntityCandidate> entities;
  entities.push_back(EntityCandidate{
      .name = "goal",
      .value = *request.goal_contract.goal_description,
      .confidence = 0.95F,
      .evidence_refs = {"goal_contract.goal_description"},
  });
  entities.push_back(EntityCandidate{
      .name = "user_turn",
      .value = *request.context_packet.user_turn,
      .confidence = 0.90F,
      .evidence_refs = {"context_packet.user_turn"},
  });

  if (request.context_packet.active_tools.has_value()) {
    for (const auto& tool_name : *request.context_packet.active_tools) {
      if (tool_name.empty()) {
        continue;
      }

      entities.push_back(EntityCandidate{
          .name = "tool",
          .value = tool_name,
          .confidence = 0.80F,
          .evidence_refs = {"context_packet.active_tools"},
      });
    }
  }

  if (request.latest_observation.has_value() &&
      request.latest_observation->observation_id.has_value()) {
    entities.push_back(EntityCandidate{
        .name = "observation",
        .value = *request.latest_observation->observation_id,
        .confidence = 0.70F,
        .evidence_refs = {"latest_observation.observation_id"},
    });
  }

  return entities;
}

[[nodiscard]] ConstraintDigest derive_constraints(const CognitionStepRequest& request) {
  ConstraintDigest digest;
  if (request.goal_contract.constraints.has_value() &&
      !request.goal_contract.constraints->empty()) {
    digest.hard_constraints.push_back(*request.goal_contract.constraints);
  }
  digest.soft_constraints.push_back(*request.goal_contract.success_criteria);

  if (request.context_packet.policy_digest.has_value() &&
      !request.context_packet.policy_digest->empty()) {
    digest.policy_refs.push_back(*request.context_packet.policy_digest);
  }

  return digest;
}

[[nodiscard]] bool is_pronoun_only_request(std::string_view user_turn) {
  const auto lowered = to_lower(user_turn);
  return lowered == "it" || lowered == "this" || lowered == "that" || lowered == "help me with it" ||
         lowered == "help me with this";
}

[[nodiscard]] std::vector<AmbiguityMarker> detect_ambiguities(const CognitionStepRequest& request,
                                                              const CognitionConfig& config,
                                                              const std::vector<EntityCandidate>& entities) {
  std::vector<AmbiguityMarker> ambiguities;
  const auto lowered_turn = to_lower(*request.context_packet.user_turn);

  if (is_pronoun_only_request(*request.context_packet.user_turn) ||
      lowered_turn.find(" maybe") != std::string::npos ||
      lowered_turn.find("something") != std::string::npos) {
    ambiguities.push_back(AmbiguityMarker{
        .ambiguity_id = "underspecified_target",
        .description = "user_turn does not identify a concrete target for cognition",
        .missing_evidence_refs = {"context_packet.user_turn"},
        .severity = std::max(0.60F, config.thresholds.ask_clarification),
    });
  }

  if (request.belief_state.hypotheses.has_value() &&
      !request.belief_state.hypotheses->empty() &&
      request.belief_state.confidence.value_or(0.0F) <= config.thresholds.ask_clarification) {
    auto missing_refs = std::vector<std::string>{};
    if (request.belief_state.evidence_refs.has_value() && request.belief_state.evidence_refs->empty()) {
      missing_refs.push_back("belief_state.evidence_refs");
    }
    ambiguities.push_back(AmbiguityMarker{
        .ambiguity_id = "belief_confidence_low",
        .description = "belief_state confidence is too low to treat current hypotheses as stable",
        .missing_evidence_refs = std::move(missing_refs),
        .severity = std::max(0.65F, 1.0F - request.belief_state.confidence.value_or(0.0F)),
    });
  }

  constexpr std::array<std::string_view, 5> kActionKeywords = {"search", "find", "check", "collect",
                                                                "lookup"};
  if (!request.context_packet.active_tools.has_value() &&
      contains_any(*request.context_packet.user_turn, kActionKeywords) && entities.size() < 3U) {
    ambiguities.push_back(AmbiguityMarker{
        .ambiguity_id = "tool_route_unspecified",
        .description = "request implies an external lookup but no visible tool is present",
        .missing_evidence_refs = {"context_packet.active_tools"},
        .severity = 0.55F,
    });
  }

  return ambiguities;
}

[[nodiscard]] std::vector<ClarificationCandidate> derive_clarification_questions(
    const std::vector<AmbiguityMarker>& ambiguities) {
  std::vector<ClarificationCandidate> questions;
  for (const auto& ambiguity : ambiguities) {
    if (ambiguity.ambiguity_id == "underspecified_target") {
      questions.push_back(ClarificationCandidate{
          .question = "Which concrete target should cognition act on before planning begins?",
          .evidence_refs = ambiguity.missing_evidence_refs,
          .priority = ambiguity.severity,
      });
      continue;
    }

    if (ambiguity.ambiguity_id == "belief_confidence_low") {
      questions.push_back(ClarificationCandidate{
          .question = "What evidence should confirm the current hypothesis before execution continues?",
          .evidence_refs = ambiguity.missing_evidence_refs,
          .priority = ambiguity.severity,
      });
      continue;
    }

    if (ambiguity.ambiguity_id == "tool_route_unspecified") {
      questions.push_back(ClarificationCandidate{
          .question = "Which tool domain or data source should be used for this lookup?",
          .evidence_refs = ambiguity.missing_evidence_refs,
          .priority = ambiguity.severity,
      });
    }
  }

  std::sort(questions.begin(), questions.end(), [](const ClarificationCandidate& left,
                                                   const ClarificationCandidate& right) {
    return left.priority > right.priority;
  });
  return questions;
}

[[nodiscard]] bool should_fail_without_rule_fallback(const CognitionStepRequest& request,
                                                     const std::vector<AmbiguityMarker>& ambiguities) {
  return is_pronoun_only_request(*request.context_packet.user_turn) &&
         !request.context_packet.active_tools.has_value() && !ambiguities.empty() &&
         request.belief_state.confidence.value_or(0.0F) < 0.30F;
}

[[nodiscard]] float derive_confidence(const std::vector<EntityCandidate>& entities,
                                      const std::vector<AmbiguityMarker>& ambiguities) {
  float confidence = entities.empty() ? 0.30F : 0.85F;
  confidence -= static_cast<float>(ambiguities.size()) * 0.18F;
  return std::clamp(confidence, 0.05F, 0.95F);
}

[[nodiscard]] bool validate_perception_output(const PerceptionResult& result) {
  return !result.intent_summary.empty() && !result.task_type.empty() && result.confidence >= 0.0F &&
         result.confidence <= 1.0F &&
         (!result.requires_clarification || !result.clarification_questions.empty());
}

}  // namespace

PerceptionEngine::PerceptionEngine(CognitionConfig config) : config_(std::move(config)) {}

std::optional<PerceptionResult> PerceptionEngine::perceive(
    const CognitionStepRequest& request) const {
  if (!validation::InputBoundaryValidator::validate_decide_request(request).ok()) {
    return std::nullopt;
  }

  auto entities = extract_entities(request);
  auto ambiguities = detect_ambiguities(request, config_, entities);
  if (!config_.perception.rule_fallback_enabled &&
      should_fail_without_rule_fallback(request, ambiguities)) {
    return std::nullopt;
  }

  PerceptionResult result;
  result.intent_summary = summarize_intent(request);
  result.task_type = derive_task_type(request);
  result.entities = std::move(entities);
  result.constraints_digest = derive_constraints(request);
  result.ambiguities = std::move(ambiguities);
  result.clarification_questions = derive_clarification_questions(result.ambiguities);
  result.requires_clarification = !result.clarification_questions.empty();
  result.confidence = derive_confidence(result.entities, result.ambiguities);
  if (result.requires_clarification) {
    result.confidence = std::min(result.confidence,
                                 std::max(0.05F, config_.thresholds.ask_clarification - 0.05F));
  }
  result.diagnostics.push_back(config_.perception.rule_fallback_enabled
                                   ? std::string("perception.rule_fallback")
                                   : std::string("perception.no_rule_fallback"));
  result.diagnostics.push_back(result.requires_clarification
                                   ? std::string("perception.requires_clarification")
                                   : std::string("perception.actionable"));

  if (!validate_perception_output(result)) {
    return std::nullopt;
  }

  return result;
}

}  // namespace dasall::cognition::perception