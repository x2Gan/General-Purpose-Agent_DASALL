#include <exception>
#include <iostream>
#include <string>

#include "CognitionTypes.h"
#include "support/TestAssertions.h"
#include "validation/InputBoundaryValidator.h"

namespace {

using dasall::cognition::CognitionStepRequest;
using dasall::cognition::validation::InputBoundaryValidator;
using dasall::contracts::GoalStatus;
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
  goal_contract.goal_id = std::string("goal-input-boundary");
  goal_contract.request_id = std::string("req-input-boundary");
  goal_contract.goal_description = std::string("validate cognition entry boundary");
  goal_contract.success_criteria =
      std::string("reject incomplete cognition decide requests");
  goal_contract.status = GoalStatus::Active;
  goal_contract.created_at = 1712345600000;
  return goal_contract;
}

[[nodiscard]] dasall::contracts::ContextPacket make_context_packet() {
  dasall::contracts::ContextPacket context_packet;
  context_packet.request_id = std::string("req-input-boundary");
  context_packet.user_turn = std::string("validate this cognition request");
  context_packet.current_goal_summary =
      std::string("ensure input boundary checks fail closed");
  context_packet.recent_history =
      std::vector<std::string>{std::string("user requested a guarded cognition flow")};
  return context_packet;
}

[[nodiscard]] dasall::contracts::BeliefState make_belief_state() {
  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-input-boundary");
  belief_state.confirmed_facts =
      std::vector<std::string>{std::string("input validation must run first")};
  belief_state.hypotheses =
      std::vector<std::string>{std::string("missing fields should be explicit")};
  belief_state.assumptions =
      std::vector<std::string>{std::string("runtime provides canonical ids")};
  belief_state.evidence_refs =
      std::vector<std::string>{std::string("cognition:input-boundary")};
  belief_state.confidence = 0.85F;
  belief_state.goal_id = std::string("goal-input-boundary");
  belief_state.created_at = 1712345600100;
  return belief_state;
}

[[nodiscard]] CognitionStepRequest make_decide_request() {
  CognitionStepRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-input-boundary";
  request.trace_id = "trace-input-boundary";
  request.profile_id = "desktop_full";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  return request;
}

void test_validate_decide_request_accepts_complete_boundary_objects() {
  const auto validation_result =
      InputBoundaryValidator::validate_decide_request(make_decide_request());

  assert_true(validation_result.ok(),
              "complete decide boundary input should pass the boundary validator");
  assert_true(validation_result.missing_fields.empty(),
              "complete decide boundary input should not report missing fields");
  assert_true(!validation_result.error_info.has_value(),
              "successful validation should not synthesize ErrorInfo");
}

void test_validate_decide_request_rejects_missing_goal_contract_fields() {
  auto request = make_decide_request();
  request.goal_contract.goal_description.reset();
  request.goal_contract.success_criteria.reset();

  const auto validation_result = InputBoundaryValidator::validate_decide_request(request);

  assert_true(!validation_result.ok(),
              "missing goal contract fields should fail decide validation");
  assert_true(validation_result.error_info.has_value(),
              "failed goal contract validation should return ErrorInfo");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               validation_result.error_info->details.code.value_or(0),
               "goal contract validation should use the canonical missing-field result code");
  assert_true(contains_field(validation_result.missing_fields,
                             "goal_contract.goal_description"),
              "goal boundary validation should report the missing description");
  assert_true(contains_field(validation_result.missing_fields,
                             "goal_contract.success_criteria"),
              "goal boundary validation should report the missing success criteria");
}

void test_validate_decide_request_rejects_missing_context_packet_fields() {
  auto request = make_decide_request();
  request.context_packet.user_turn.reset();
  request.context_packet.recent_history.reset();

  const auto validation_result = InputBoundaryValidator::validate_decide_request(request);

  assert_true(!validation_result.ok(),
              "missing context packet fields should fail decide validation");
  assert_true(validation_result.error_info.has_value(),
              "failed context packet validation should return ErrorInfo");
  assert_true(contains_field(validation_result.missing_fields,
                             "context_packet.user_turn"),
              "context boundary validation should report the missing user turn");
  assert_true(contains_field(validation_result.missing_fields,
                             "context_packet.recent_history"),
              "context boundary validation should report the missing recent history");
}

void test_validate_decide_request_rejects_missing_belief_state_fields() {
  auto request = make_decide_request();
  request.belief_state.confirmed_facts.reset();
  request.belief_state.confidence.reset();

  const auto validation_result = InputBoundaryValidator::validate_decide_request(request);

  assert_true(!validation_result.ok(),
              "missing belief state fields should fail decide validation");
  assert_true(validation_result.error_info.has_value(),
              "failed belief state validation should return ErrorInfo");
  assert_true(contains_field(validation_result.missing_fields,
                             "belief_state.confirmed_facts"),
              "belief boundary validation should report missing confirmed facts");
  assert_true(contains_field(validation_result.missing_fields,
                             "belief_state.confidence"),
              "belief boundary validation should report missing confidence");
}

void test_validate_decide_request_rejects_injection_detected_input_safety_signal() {
  auto request = make_decide_request();
  request.execution_hints.input_safety_signal = dasall::contracts::InputSafetySignal{
      .injection_detected = true,
      .pii_detected = false,
      .reason_codes = std::vector<std::string>{"prompt_injection_phrase_detected"},
  };

  const auto validation_result = InputBoundaryValidator::validate_decide_request(request);

  assert_true(!validation_result.ok(),
              "injection-detected safety signals should fail closed at decide validation");
  assert_true(validation_result.error_info.has_value(),
              "policy-denied boundary failures should surface ErrorInfo");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::PolicyDenied),
               validation_result.error_info->details.code.value_or(0),
               "input safety denial should use the canonical policy-denied result code");
  assert_true(validation_result.error_info->details.message.find(
                      "execution_hints.input_safety_signal") != std::string::npos,
              "policy-denied boundary failures should identify the blocking safety signal");
}

}  // namespace

int main() {
  try {
    test_validate_decide_request_accepts_complete_boundary_objects();
    test_validate_decide_request_rejects_missing_goal_contract_fields();
    test_validate_decide_request_rejects_missing_context_packet_fields();
    test_validate_decide_request_rejects_missing_belief_state_fields();
    test_validate_decide_request_rejects_injection_detected_input_safety_signal();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}