#include "ICognitionEngine.h"

#include <algorithm>
#include <string>
#include <utility>

#include "validation/InputBoundaryValidator.h"

namespace dasall::cognition {
namespace {

constexpr const char* kDefaultToolName = "agent.dataset";

[[nodiscard]] std::string choose_tool_name(const contracts::ContextPacket& context_packet) {
  if (context_packet.active_tools.has_value()) {
    const auto& tools = *context_packet.active_tools;
    const auto selected = std::find_if(tools.begin(), tools.end(), [](const std::string& tool) {
      return !tool.empty();
    });
    if (selected != tools.end()) {
      return *selected;
    }
  }

  return kDefaultToolName;
}

[[nodiscard]] bool should_recommend_context_reload(
    const validation::InputBoundaryValidationResult& validation_result) {
  return std::any_of(validation_result.missing_fields.begin(),
                     validation_result.missing_fields.end(),
                     [](const std::string& field_name) {
                       return field_name.starts_with("context_packet.") ||
                              field_name.starts_with("belief_state.") ||
                              field_name.starts_with("latest_observation.");
                     });
}

void apply_invalid_decide_result(
    CognitionDecisionResult& result,
    const validation::InputBoundaryValidationResult& validation_result) {
  result.result_code = contracts::ResultCode::ValidationFieldMissing;
  result.error_info = validation_result.error_info;
  result.context_sufficiency.context_sufficient = false;
  result.context_sufficiency.context_confidence = 0.0F;
  result.context_sufficiency.missing_evidence_hints = validation_result.missing_fields;
  result.context_sufficiency.recommend_context_reload =
      should_recommend_context_reload(validation_result);
  result.diagnostics.push_back("invalid_input");
}

void apply_invalid_reflection_result(
    CognitionReflectionResult& result,
    const validation::InputBoundaryValidationResult& validation_result) {
  result.result_code = contracts::ResultCode::ValidationFieldMissing;
  result.error_info = validation_result.error_info;
  result.diagnostics.push_back("invalid_input");
}

class CognitionFacade final : public ICognitionEngine {
 public:
  explicit CognitionFacade(CognitionConfig config) : config_(std::move(config)) {}

  [[nodiscard]] CognitionDecisionResult decide(
      const CognitionStepRequest& request) override {
    CognitionDecisionResult result;
    const auto validation_result =
        validation::InputBoundaryValidator::validate_decide_request(request);
    if (!validation_result.ok()) {
      apply_invalid_decide_result(result, validation_result);
      return result;
    }

    result.context_sufficiency.context_sufficient = true;
    result.context_sufficiency.context_confidence = 0.9F;
    decision::ActionDecision action_decision;
    action_decision.decision_kind = decision::ActionDecisionKind::ExecuteAction;
      action_decision.selected_node_id = std::string("plan-node:default");
    action_decision.confidence = 0.8F;
    action_decision.rationale =
        std::string("runtime true integration minimal path selects a visible tool");
      action_decision.tool_intent_hint = decision::ToolIntentHint{
        .tool_name = choose_tool_name(request.context_packet),
        .intent_summary = std::string("query current user turn through runtime tool governance"),
        .argument_hints = {std::string("query=") + *request.context_packet.user_turn},
        .evidence_refs = {"cognition:decide"},
      };
      action_decision.candidate_scores = {
        decision::CandidateDecisionScore{
          .candidate_name = "execute_action",
          .score = 0.8F,
          .rationale = std::string("tool route is available and context is sufficient"),
        },
        decision::CandidateDecisionScore{
          .candidate_name = "direct_response",
          .score = 0.35F,
          .rationale = std::string("user turn implies a governed lookup before final response"),
        },
      };
    result.action_decision = action_decision;
    result.belief_update_hint = belief::BeliefUpdateHint{
        .confirmed_facts_delta = {
          belief::FactDelta{.fact = "cognition decision path executed"},
        },
      .hypotheses_delta = {},
      .assumptions_delta = {},
        .evidence_refs_delta = {
          belief::EvidenceRefDelta{.evidence_ref = "cognition:decide"},
        },
      .missing_evidence_refs = {},
        .confidence_hint = 0.8F,
        .merge_mode = belief::BeliefMergeMode::Append,
    };
    return result;
  }

  [[nodiscard]] CognitionReflectionResult reflect(
      const ReflectionRequest& request) override {
    CognitionReflectionResult result;
    const auto validation_result =
        validation::InputBoundaryValidator::validate_reflection_request(request);
    if (!validation_result.ok()) {
      apply_invalid_reflection_result(result, validation_result);
      return result;
    }

    contracts::ReflectionDecision reflection_decision;
    reflection_decision.request_id = request.request_id;
    reflection_decision.goal_id = request.goal_contract.goal_id;
    reflection_decision.decision_kind = request.latest_observation.success.value_or(false)
                                           ? contracts::ReflectionDecisionKind::Continue
                                           : contracts::ReflectionDecisionKind::AbortSafe;
    reflection_decision.confidence = request.latest_observation.success.value_or(false) ? 0.9F : 0.2F;
    reflection_decision.rationale = std::string("minimal cognition reflection keeps control in runtime");
    if (request.latest_observation.observation_id.has_value()) {
      reflection_decision.relevant_observation_refs =
          std::vector<std::string>{*request.latest_observation.observation_id};
    }
    reflection_decision.tags = std::vector<std::string>{"cognition", "reflection"};
    result.reflection_decision = reflection_decision;
    result.diagnostics.push_back("reflection_minimal_path");
    return result;
  }

 private:
  CognitionConfig config_;
};

}  // namespace

std::unique_ptr<ICognitionEngine> create_cognition_engine(const CognitionConfig& config) {
  return std::make_unique<CognitionFacade>(config);
}

}  // namespace dasall::cognition
