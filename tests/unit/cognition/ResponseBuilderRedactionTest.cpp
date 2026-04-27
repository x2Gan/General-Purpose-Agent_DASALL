#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "IResponseBuilder.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::ResponseBuildHints;
using dasall::cognition::ResponseBuildRequest;
using dasall::cognition::create_response_builder;
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

[[nodiscard]] bool contains_substring(const std::optional<std::string>& value,
                                      const std::string& expected) {
  return value.has_value() && value->find(expected) != std::string::npos;
}

[[nodiscard]] dasall::contracts::GoalContract make_goal_contract() {
  dasall::contracts::GoalContract goal_contract;
  goal_contract.goal_id = std::string("goal-019-redaction");
  goal_contract.request_id = std::string("req-019-redaction");
  goal_contract.goal_description =
      std::string("return a response without leaking provider-private fields");
  goal_contract.success_criteria = std::string("mask reasoning and secret-bearing fields");
  goal_contract.status = GoalStatus::Active;
  goal_contract.created_at = 1712432200000;
  return goal_contract;
}

[[nodiscard]] dasall::contracts::ContextPacket make_context_packet() {
  dasall::contracts::ContextPacket context_packet;
  context_packet.request_id = std::string("req-019-redaction");
  context_packet.user_turn = std::string("Show the safe response only");
  context_packet.current_goal_summary =
      std::string("strip provider-private and secret-bearing fields from the terminal output");
  context_packet.recent_history =
      std::vector<std::string>{std::string("runtime received a response-stage payload")};
  return context_packet;
}

[[nodiscard]] dasall::contracts::BeliefState make_belief_state() {
  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-019-redaction");
  belief_state.confirmed_facts =
      std::vector<std::string>{std::string("provider-private fields must stay local")};
  belief_state.hypotheses =
      std::vector<std::string>{std::string("response builder should redact before emitting text")};
  belief_state.assumptions =
      std::vector<std::string>{std::string("observability redaction is enabled by default")};
  belief_state.evidence_refs =
      std::vector<std::string>{std::string("obs-019-redaction")};
  belief_state.confidence = 0.91F;
  belief_state.goal_id = std::string("goal-019-redaction");
  belief_state.created_at = 1712432200100;
  return belief_state;
}

[[nodiscard]] dasall::contracts::Observation make_observation() {
  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-019-redaction");
  observation.source = ObservationSource::ToolExecution;
  observation.success = true;
  observation.payload = std::string(
      "{\"dataset\":\"agent.dataset\",\"reasoning_content\":\"private_chain\",\"api_token\":\"super-secret-token\",\"raw_prompt\":\"system secret\"}");
  observation.created_at = 1712432200200;
  observation.request_id = std::string("req-019-redaction");
  observation.goal_id = std::string("goal-019-redaction");
  return observation;
}

[[nodiscard]] ResponseBuildRequest make_request() {
  ResponseBuildRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-019-redaction";
  request.trace_id = "trace-019-redaction";
  request.profile_id = "desktop_full";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  request.latest_observation = make_observation();
  request.build_hints = ResponseBuildHints{
      .prefer_template = false,
      .allow_template_fallback = true,
      .max_summary_chars = 0U,
      .required_sections = {std::string("summary"), std::string("redaction")},
  };
  return request;
}

void test_response_builder_redacts_provider_private_fields_by_default() {
  auto builder = create_response_builder();

  const auto result = builder->build(make_request());

  assert_true(result.agent_result.has_value(),
              "redaction path should still materialize an AgentResult");
  assert_true(contains_token(result.diagnostics, "response_redacted"),
              "redaction path should emit the response_redacted diagnostic");

  const auto& agent_result = *result.agent_result;
  assert_true(contains_substring(agent_result.response_text, "\"dataset\":\"agent.dataset\""),
              "redaction should preserve non-sensitive structured payload fields");
  assert_true(!contains_substring(agent_result.response_text, "private_chain"),
              "redaction should remove reasoning_content values from response_text");
  assert_true(!contains_substring(agent_result.response_text, "super-secret-token"),
              "redaction should remove api_token values from response_text");
  assert_true(!contains_substring(agent_result.response_text, "system secret"),
              "redaction should remove raw_prompt values from response_text");
  assert_true(contains_substring(agent_result.response_text, "[REDACTED]"),
              "redaction should leave an explicit placeholder instead of silently dropping content");
  assert_true(agent_result.tags.has_value() && contains_token(*agent_result.tags, "response_redacted"),
              "redaction should be traceable via AgentResult tags");
  assert_true(agent_result.structured_payload.has_value() &&
                  agent_result.structured_payload->find("redacted:reasoning_content") !=
                      std::string::npos,
              "structured payload should retain omitted_details for the masked reasoning content");
}

void test_response_builder_can_preserve_payload_when_redaction_is_explicitly_disabled() {
  CognitionConfig config;
  config.observability.redact_context_payload = false;
  auto builder = create_response_builder(config);

  const auto result = builder->build(make_request());

  assert_true(result.agent_result.has_value(),
              "disabling redaction should still materialize an AgentResult");
  assert_true(!contains_token(result.diagnostics, "response_redacted"),
              "disabling redaction should suppress the response_redacted diagnostic");

  const auto& agent_result = *result.agent_result;
  assert_true(contains_substring(agent_result.response_text, "private_chain"),
              "disabling redaction should preserve reasoning_content for explicit opt-out coverage");
  assert_true(contains_substring(agent_result.response_text, "super-secret-token"),
              "disabling redaction should preserve api_token values");
}

}  // namespace

int main() {
  try {
    test_response_builder_redacts_provider_private_fields_by_default();
    test_response_builder_can_preserve_payload_when_redaction_is_explicitly_disabled();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}