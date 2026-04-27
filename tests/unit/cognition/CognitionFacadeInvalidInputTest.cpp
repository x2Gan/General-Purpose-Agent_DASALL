#include <exception>
#include <iostream>
#include <string>

#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionStepRequest;
using dasall::cognition::ReflectionRequest;
using dasall::cognition::ResponseBuildRequest;
using dasall::cognition::create_cognition_engine;
using dasall::cognition::create_response_builder;
using dasall::contracts::GoalStatus;
using dasall::contracts::ObservationSource;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_field(const std::vector<std::string>& fields,
                                  const std::string& expected_field) {
  for (const auto& field : fields) {
    if (field == expected_field) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] dasall::contracts::GoalContract make_goal_contract() {
  dasall::contracts::GoalContract goal_contract;
  goal_contract.goal_id = std::string("goal-013");
  goal_contract.request_id = std::string("req-013");
  goal_contract.goal_description =
      std::string("keep invalid cognition inputs out of downstream stages");
  goal_contract.success_criteria = std::string("return explicit validation errors");
  goal_contract.status = GoalStatus::Active;
  goal_contract.created_at = 1712345600000;
  return goal_contract;
}

[[nodiscard]] dasall::contracts::ContextPacket make_context_packet() {
  dasall::contracts::ContextPacket context_packet;
  context_packet.request_id = std::string("req-013");
  context_packet.user_turn = std::string("reason over guarded input");
  context_packet.current_goal_summary =
      std::string("fail fast when cognition boundaries are incomplete");
  context_packet.recent_history =
      std::vector<std::string>{std::string("runtime passed a cognition request")};
  return context_packet;
}

[[nodiscard]] dasall::contracts::BeliefState make_belief_state() {
  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-013");
  belief_state.confirmed_facts = std::vector<std::string>{
      std::string("boundary validation runs before reasoning")};
  belief_state.hypotheses = std::vector<std::string>{
      std::string("missing context should produce a validation failure")};
  belief_state.assumptions = std::vector<std::string>{
      std::string("runtime request ids are consistent across layers")};
  belief_state.evidence_refs =
      std::vector<std::string>{std::string("cognition:invalid-input")};
  belief_state.confidence = 0.80F;
  belief_state.goal_id = std::string("goal-013");
  belief_state.created_at = 1712345600100;
  return belief_state;
}

[[nodiscard]] dasall::contracts::Observation make_observation() {
  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-013");
  observation.source = ObservationSource::ToolExecution;
  observation.success = true;
  observation.payload = std::string("{\"status\":\"ok\"}");
  observation.created_at = 1712345600200;
  observation.request_id = std::string("req-013");
  observation.goal_id = std::string("goal-013");
  return observation;
}

[[nodiscard]] CognitionStepRequest make_decide_request() {
  CognitionStepRequest request;
  request.caller_domain = "cognition.tests";
  request.request_id = "req-013";
  request.trace_id = "trace-013";
  request.profile_id = "desktop_full";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  request.latest_observation = make_observation();
  return request;
}

[[nodiscard]] ReflectionRequest make_reflection_request() {
  ReflectionRequest request;
  request.caller_domain = "cognition.tests";
  request.request_id = "req-013";
  request.trace_id = "trace-013";
  request.profile_id = "desktop_full";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  request.latest_observation = make_observation();
  return request;
}

[[nodiscard]] ResponseBuildRequest make_response_request() {
  ResponseBuildRequest request;
  request.caller_domain = "cognition.tests";
  request.request_id = "req-013";
  request.trace_id = "trace-013";
  request.profile_id = "desktop_full";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  request.latest_observation = make_observation();
  return request;
}

void test_decide_surfaces_explicit_validation_error_for_invalid_boundary_input() {
  auto engine = create_cognition_engine();
  auto request = make_decide_request();
  request.context_packet.user_turn.reset();
  request.belief_state.evidence_refs.reset();

  const auto result = engine->decide(request);

  assert_true(result.result_code.has_value(),
              "invalid decide input should set a cognition result code");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(*result.result_code),
               "decide should use the canonical validation result code");
  assert_true(result.error_info.has_value(),
              "invalid decide input should emit an explicit ErrorInfo payload");
  assert_equal(std::string("cognition.decide.validation"),
               result.error_info->details.stage,
               "decide should stamp the validation stage name into ErrorInfo");
  assert_true(!result.context_sufficiency.context_sufficient,
              "invalid decide input should mark context as insufficient");
  assert_true(result.context_sufficiency.recommend_context_reload,
              "missing context or belief evidence should request a context reload");
  assert_true(contains_field(result.context_sufficiency.missing_evidence_hints,
                             "context_packet.user_turn"),
              "decide should surface the missing user_turn hint explicitly");
  assert_true(contains_field(result.context_sufficiency.missing_evidence_hints,
                             "belief_state.evidence_refs"),
              "decide should surface the missing belief evidence hint explicitly");
  assert_true(!result.action_decision.has_value(),
              "invalid decide input should fail fast instead of synthesizing an action decision");
}

void test_reflect_surfaces_explicit_validation_error_for_missing_observation_payload() {
  auto engine = create_cognition_engine();
  auto request = make_reflection_request();
  request.latest_observation.payload.reset();

  const auto result = engine->reflect(request);

  assert_true(result.result_code.has_value(),
              "invalid reflection input should set a cognition result code");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(*result.result_code),
               "reflection should use the canonical validation result code");
  assert_true(result.error_info.has_value(),
              "invalid reflection input should emit an explicit ErrorInfo payload");
  assert_equal(std::string("cognition.reflect.validation"),
               result.error_info->details.stage,
               "reflection should stamp the validation stage name into ErrorInfo");
  assert_true(!result.reflection_decision.has_value(),
              "invalid reflection input should fail fast instead of synthesizing a reflection decision");
}

void test_response_builder_surfaces_explicit_validation_error_for_invalid_context_boundary() {
  auto builder = create_response_builder();
  auto request = make_response_request();
  request.context_packet.current_goal_summary.reset();

  const auto result = builder->build(request);

  assert_true(result.result_code.has_value(),
              "invalid response input should set a result code");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(*result.result_code),
               "response builder should use the canonical validation result code");
  assert_true(result.error_info.has_value(),
              "invalid response input should emit an explicit ErrorInfo payload");
  assert_equal(std::string("cognition.response.validation"),
               result.error_info->details.stage,
               "response builder should stamp the validation stage name into ErrorInfo");
  assert_true(!result.agent_result.has_value(),
              "invalid response input should fail fast instead of producing a degraded agent result");
}

}  // namespace

int main() {
  try {
    test_decide_surfaces_explicit_validation_error_for_invalid_boundary_input();
    test_reflect_surfaces_explicit_validation_error_for_missing_observation_payload();
    test_response_builder_surfaces_explicit_validation_error_for_invalid_context_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}