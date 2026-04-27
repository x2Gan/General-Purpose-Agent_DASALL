#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "IResponseBuilder.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ResponseBuildHints;
using dasall::cognition::ResponseBuildRequest;
using dasall::cognition::create_response_builder;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::GoalStatus;
using dasall::contracts::ObservationSource;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_token(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] dasall::contracts::GoalContract make_goal_contract() {
  dasall::contracts::GoalContract goal_contract;
  goal_contract.goal_id = std::string("goal-019-mapping");
  goal_contract.request_id = std::string("req-019-mapping");
  goal_contract.goal_description =
      std::string("return the normalized dataset summary to runtime");
  goal_contract.success_criteria = std::string("emit a completed AgentResult");
  goal_contract.status = GoalStatus::Active;
  goal_contract.created_at = 1712432000000;
  return goal_contract;
}

[[nodiscard]] dasall::contracts::ContextPacket make_context_packet() {
  dasall::contracts::ContextPacket context_packet;
  context_packet.request_id = std::string("req-019-mapping");
  context_packet.user_turn = std::string("Summarize the dataset response for the user");
  context_packet.current_goal_summary =
      std::string("project the governed dataset payload into a completed result");
  context_packet.recent_history =
      std::vector<std::string>{std::string("runtime completed a builtin dataset query")};
  return context_packet;
}

[[nodiscard]] dasall::contracts::BeliefState make_belief_state() {
  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-019-mapping");
  belief_state.confirmed_facts =
      std::vector<std::string>{std::string("agent.dataset returned a structured payload")};
  belief_state.hypotheses =
      std::vector<std::string>{std::string("response builder can project the payload")};
  belief_state.assumptions =
      std::vector<std::string>{std::string("runtime will submit the final result")};
  belief_state.evidence_refs =
      std::vector<std::string>{std::string("obs-019-mapping")};
  belief_state.confidence = 0.93F;
  belief_state.goal_id = std::string("goal-019-mapping");
  belief_state.created_at = 1712432000100;
  return belief_state;
}

[[nodiscard]] dasall::contracts::Observation make_observation() {
  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-019-mapping");
  observation.source = ObservationSource::ToolExecution;
  observation.success = true;
  observation.payload = std::string("{\"dataset\":\"agent.dataset\",\"status\":\"ok\"}");
  observation.created_at = 1712432000200;
  observation.request_id = std::string("req-019-mapping");
  observation.goal_id = std::string("goal-019-mapping");
  return observation;
}

[[nodiscard]] dasall::cognition::decision::ActionDecision make_terminal_decision() {
  dasall::cognition::decision::ActionDecision decision;
  decision.decision_kind = dasall::cognition::decision::ActionDecisionKind::DirectResponse;
  decision.confidence = 0.88F;
  decision.rationale = std::string("runtime already holds a complete dataset payload");
  decision.response_outline = dasall::cognition::decision::ResponseOutline{
      .summary = std::string("dataset summary ready for the user"),
      .key_points = {std::string("dataset ready"), std::string("projection succeeds")},
  };
  return decision;
}

[[nodiscard]] ResponseBuildRequest make_request() {
  ResponseBuildRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-019-mapping";
  request.trace_id = "trace-019-mapping";
  request.profile_id = "desktop_full";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  request.latest_observation = make_observation();
  request.terminal_decision = make_terminal_decision();
  request.build_hints = ResponseBuildHints{
      .prefer_template = false,
      .allow_template_fallback = true,
      .max_summary_chars = 0U,
      .required_sections = {std::string("summary"), std::string("evidence")},
  };
  return request;
}

void test_response_builder_maps_observation_payload_to_completed_agent_result() {
  auto builder = create_response_builder();

  const auto result = builder->build(make_request());

  assert_true(result.agent_result.has_value(),
              "completed response path should materialize an AgentResult");
  assert_true(!result.fallback_used,
              "completed payload projection should not mark template fallback");
  assert_true(contains_token(result.diagnostics, "response_mode:llm_projection"),
              "completed response path should expose the llm projection diagnostic");

  const auto& agent_result = *result.agent_result;
  assert_true(agent_result.status == AgentResultStatus::Completed,
              "payload projection should complete the AgentResult");
  assert_true(agent_result.task_completed.value_or(false),
              "completed AgentResult should set task_completed=true");
  assert_true(agent_result.response_text.has_value() &&
                  agent_result.response_text->find("runtime unary integration completed:") !=
                      std::string::npos,
              "response text should preserve the runtime unary integration completion prefix");
  assert_true(agent_result.response_text.has_value() &&
                  agent_result.response_text->find("\"dataset\":\"agent.dataset\"") !=
                      std::string::npos,
              "response text should retain the structured dataset payload");
  assert_true(agent_result.structured_payload.has_value() &&
                  agent_result.structured_payload->find("\"response_mode\":\"llm_projection\"") !=
                      std::string::npos,
              "structured payload should record the llm projection response mode");
  assert_true(agent_result.tags.has_value() &&
                  contains_token(*agent_result.tags, "response_mode:llm_projection"),
              "AgentResult tags should preserve the response mode for downstream traces");
}

}  // namespace

int main() {
  try {
    test_response_builder_maps_observation_payload_to_completed_agent_result();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}