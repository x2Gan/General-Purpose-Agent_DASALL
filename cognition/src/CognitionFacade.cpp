#include "ICognitionEngine.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "IResponseBuilder.h"

namespace dasall::cognition {
namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::string choose_tool_name(const CognitionConfig& config,
                                           const contracts::ContextPacket& context_packet) {
  if (context_packet.active_tools.has_value()) {
    const auto& tools = *context_packet.active_tools;
    const auto selected = std::find_if(tools.begin(), tools.end(), [](const std::string& tool) {
      return !tool.empty();
    });
    if (selected != tools.end()) {
      return *selected;
    }
  }

  return config.default_tool_name;
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
  result.goal_id = request.goal_id;
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
      result.context_sufficiency.context_confidence = 0.0F;
      result.context_sufficiency.recommend_context_reload = true;
      result.context_sufficiency.missing_evidence_hints = {"user_turn", "current_goal_summary"};
      result.action_decision.decision_kind = decision::ActionDecisionKind::AskClarification;
      result.action_decision.confidence = 1.0F;
      result.action_decision.clarification_question =
          std::string("additional goal or user input is required before execution can continue");
      result.action_decision.rationale =
          std::string("context_packet is missing the minimal fields required for true integration");
      result.diagnostics.push_back("context_packet_missing_required_fields");
      return result;
    }

    result.context_sufficiency.context_confidence = 0.9F;
    result.action_decision.decision_kind = decision::ActionDecisionKind::ExecuteAction;
    result.action_decision.confidence = 0.8F;
    result.action_decision.tool_name = choose_tool_name(config_, request.context_packet);
    result.action_decision.tool_arguments_payload =
        std::string("{\"query\":\"") + *request.context_packet.user_turn + "\"}";
    result.action_decision.rationale =
        std::string("runtime true integration minimal path selects a visible tool");
    result.action_decision.evidence_refs = std::vector<std::string>{"cognition:decide"};
    result.belief_update_hint = belief::BeliefUpdateHint{
        .confirmed_facts = {"cognition decision path executed"},
        .evidence_refs = {"cognition:decide"},
        .merge_mode = "append",
    };
    return result;
  }

  [[nodiscard]] CognitionReflectionResult reflect(
      const ReflectionRequest& request) override {
    CognitionReflectionResult result;
    result.action_decision.decision_kind = decision::ActionDecisionKind::ConvergeSafe;
    result.action_decision.confidence = request.latest_observation.success.value_or(false) ? 0.9F : 0.2F;
    result.action_decision.rationale = std::string("minimal cognition reflection keeps control in runtime");
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

    const auto fallback_text = request.action_decision.has_value() &&
                                       request.action_decision->response_text.has_value()
                                   ? *request.action_decision->response_text
                                   : std::string("runtime unary integration completed without observation payload");
    result.agent_result = make_result(
        request,
        contracts::AgentResultStatus::PartiallyCompleted,
        0,
        fallback_text);
    result.fallback_used = true;
    result.diagnostics.push_back(std::string("response_template_fallback:") + config_.default_tool_name);
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