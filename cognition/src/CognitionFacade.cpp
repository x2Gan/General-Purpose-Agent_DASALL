#include "ICognitionEngine.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "IResponseBuilder.h"

namespace dasall::cognition {
namespace {

constexpr const char* kDefaultToolName = "agent.dataset";

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] contracts::ErrorInfo make_cognition_error(
    contracts::ResultCode code,
    std::string message,
    std::string stage) {
  const auto stage_ref = stage;
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(code),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition",
          .ref_id = stage_ref,
      },
  };
}

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

[[nodiscard]] contracts::AgentResult make_result(const ResponseBuildRequest& request,
                                                 contracts::AgentResultStatus status,
                                                 std::int32_t result_code,
                                                 std::string response_text) {
  contracts::AgentResult result;
  result.result_id = request.request_id + "-cognition-response";
  result.status = status;
  result.result_code = result_code;
  result.response_text = std::move(response_text);
  result.task_completed = (status == contracts::AgentResultStatus::Completed);
  result.created_at = current_time_ms();
  result.request_id = request.request_id;
  result.trace_id = request.trace_id;
  result.goal_id = request.goal_contract.goal_id;
  result.tags = std::vector<std::string>{"cognition", "response_builder"};
  return result;
}

class CognitionFacade final : public ICognitionEngine {
 public:
  explicit CognitionFacade(CognitionConfig config) : config_(std::move(config)) {}

  [[nodiscard]] CognitionDecisionResult decide(
      const CognitionStepRequest& request) override {
    CognitionDecisionResult result;
    result.context_sufficiency.context_sufficient =
        request.context_packet.user_turn.has_value() &&
        request.context_packet.current_goal_summary.has_value();

    if (!result.context_sufficiency.context_sufficient) {
      result.result_code = contracts::ResultCode::ValidationFieldMissing;
      result.error_info = make_cognition_error(
          contracts::ResultCode::ValidationFieldMissing,
          "context_packet is missing user_turn or current_goal_summary",
          "cognition.decide");
      result.context_sufficiency.context_confidence = 0.0F;
      result.context_sufficiency.recommend_context_reload = true;
      result.context_sufficiency.missing_evidence_hints = {"user_turn", "current_goal_summary"};
      decision::ActionDecision action_decision;
      action_decision.decision_kind = decision::ActionDecisionKind::AskClarification;
      action_decision.confidence = 1.0F;
        action_decision.clarification_needed = true;
      action_decision.clarification_question =
          std::string("additional goal or user input is required before execution can continue");
      action_decision.rationale =
          std::string("context_packet is missing the minimal fields required for true integration");
        action_decision.candidate_scores = {
          decision::CandidateDecisionScore{
            .candidate_name = "ask_clarification",
            .score = 1.0F,
            .rationale = std::string("required context fields are missing"),
          },
        };
      result.action_decision = action_decision;
      result.diagnostics.push_back("context_packet_missing_required_fields");
      return result;
    }

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

class ResponseBuilder final : public IResponseBuilder {
 public:
  explicit ResponseBuilder(CognitionConfig config) : config_(std::move(config)) {}

  [[nodiscard]] ResponseBuildResult build(
      const ResponseBuildRequest& request) override {
    ResponseBuildResult result;

    if (request.latest_observation.has_value() &&
        request.latest_observation->payload.has_value()) {
      result.agent_result = make_result(
          request,
          contracts::AgentResultStatus::Completed,
          0,
          std::string("runtime unary integration completed: ") +
              *request.latest_observation->payload);
      return result;
    }

    const auto fallback_text = request.terminal_decision.has_value() &&
                     request.terminal_decision->response_outline.has_value()
                   ? request.terminal_decision->response_outline->summary
                                   : std::string("runtime unary integration completed without observation payload");
    result.agent_result = make_result(
        request,
        contracts::AgentResultStatus::PartiallyCompleted,
        0,
        fallback_text);
    result.fallback_used = true;
    result.diagnostics.push_back(
        config_.response.template_fallback_enabled
            ? std::string("response_template_fallback_enabled")
            : std::string("response_template_fallback_disabled_but_minimal_result_returned"));
    return result;
  }

 private:
  CognitionConfig config_;
};

}  // namespace

std::unique_ptr<ICognitionEngine> create_cognition_engine(const CognitionConfig& config) {
  return std::make_unique<CognitionFacade>(config);
}

std::unique_ptr<IResponseBuilder> create_response_builder(const CognitionConfig& config) {
  return std::make_unique<ResponseBuilder>(config);
}

}  // namespace dasall::cognition
