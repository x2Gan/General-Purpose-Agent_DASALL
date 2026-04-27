#include <exception>
#include <iostream>
#include <string>

#include "CognitionTypes.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"
#include "validation/InputBoundaryValidator.h"

namespace {

using dasall::cognition::CognitionStepRequest;
using dasall::cognition::ReflectionRequest;
using dasall::cognition::validation::InputBoundaryValidator;
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
      std::string("produce a validated cognition response");
  goal_contract.success_criteria =
      std::string("return an explicit validation error when input is incomplete");
  goal_contract.status = GoalStatus::Active;
  goal_contract.created_at = 1712345600000;
  return goal_contract;
}

[[nodiscard]] dasall::contracts::ContextPacket make_context_packet() {
  dasall::contracts::ContextPacket context_packet;
  context_packet.request_id = std::string("req-013");
  context_packet.user_turn = std::string("validate the boundary before reasoning");
  context_packet.current_goal_summary =
      std::string("guard cognition against incomplete input");
  context_packet.recent_history =
      std::vector<std::string>{std::string("user asked for guarded reasoning")};
  return context_packet;
}

[[nodiscard]] dasall::contracts::BeliefState make_belief_state() {
  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-013");
  belief_state.confirmed_facts =
      std::vector<std::string>{std::string("input boundary checks are required")};
  belief_state.hypotheses = std::vector<std::string>{
      std::string("explicit errors improve downstream observability")};
  belief_state.assumptions =
      std::vector<std::string>{std::string("runtime provides canonical request ids")};
  belief_state.evidence_refs = std::vector<std::string>{std::string("cognition:boundary")};
  belief_state.confidence = 0.85F;
  belief_state.goal_id = std::string("goal-013");
  belief_state.created_at = 1712345600100;
  return belief_state;
}

[[nodiscard]] dasall::contracts::Observation make_observation() {
  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-013");
  observation.source = ObservationSource::ToolExecution;
  observation.success = true;
  observation.payload = std::string("{\"validated\":true}");
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

void test_validate_decide_request_accepts_complete_boundary_objects() {
  const auto validation_result = InputBoundaryValidator::validate_decide_request(
      make_decide_request());

  assert_true(validation_result.ok(),
              "complete decide boundary input should pass the boundary validator");
  assert_true(validation_result.missing_fields.empty(),
              "no fields should be reported missing for a complete decide request");
  assert_true(!validation_result.error_info.has_value(),
              "passing boundary validation should not synthesize an ErrorInfo payload");
}

void test_validate_decide_request_rejects_missing_goal_context_and_belief_fields() {
  auto request = make_decide_request();
  request.trace_id.clear();
  request.goal_contract.goal_description.reset();
  request.context_packet.user_turn.reset();
  request.belief_state.confidence.reset();

  const auto validation_result = InputBoundaryValidator::validate_decide_request(request);

  assert_true(!validation_result.ok(),
              "missing top-level, goal, context, and belief fields should fail decide validation");
  assert_true(validation_result.error_info.has_value(),
              "failed boundary validation should return an explicit ErrorInfo payload");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               validation_result.error_info->details.code.value_or(0),
               "boundary validation should use the canonical validation result code");
  assert_true(contains_field(validation_result.missing_fields, "trace_id"),
              "top-level request metadata should be validated explicitly");
  assert_true(contains_field(validation_result.missing_fields,
                             "goal_contract.goal_description"),
              "goal boundary validation should report the missing goal description");
  assert_true(contains_field(validation_result.missing_fields,
                             "context_packet.user_turn"),
              "context boundary validation should report the missing user turn");
  assert_true(contains_field(validation_result.missing_fields,
                             "belief_state.confidence"),
              "belief boundary validation should report the missing confidence slot");
}

void test_validate_reflection_request_requires_complete_observation_boundary() {
  auto request = make_reflection_request();
  request.latest_observation.payload.reset();
  request.latest_observation.observation_id.reset();

  const auto validation_result = InputBoundaryValidator::validate_reflection_request(request);

  assert_true(!validation_result.ok(),
              "reflection validation should fail closed when the latest observation is incomplete");
  assert_true(validation_result.error_info.has_value(),
              "reflection validation should emit an explicit ErrorInfo payload");
  assert_equal(std::string("cognition.reflect.validation"),
               validation_result.error_info->details.stage,
               "reflection validation should stamp the canonical stage name into ErrorInfo");
  assert_true(contains_field(validation_result.missing_fields,
                             "latest_observation.observation_id"),
              "observation validation should report a missing observation id");
  assert_true(contains_field(validation_result.missing_fields,
                             "latest_observation.payload"),
              "observation validation should report a missing payload");
}

}  // namespace

int main() {
  try {
    test_validate_decide_request_accepts_complete_boundary_objects();
    test_validate_decide_request_rejects_missing_goal_context_and_belief_fields();
    test_validate_reflection_request_requires_complete_observation_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}