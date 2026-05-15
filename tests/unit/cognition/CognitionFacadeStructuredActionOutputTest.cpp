#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "CognitionConfig.h"
#include "decision/ActionDecision.h"
#include "error/ResultCode.h"
#include "support/TestAssertions.h"
#include "MockCognitionFixture.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] std::string make_structured_planning_payload() {
  return R"({"schema_version":"cognition.plan.v1","plan_id":"plan-structured-bridge","revision":1,"nodes":[{"node_id":"bridge-plan-node","objective":"collect governed evidence from the dataset tool","success_signal":"evidence_collected","action_kind_hint":"tool_action","depends_on":[],"evidence_refs":["belief:evidence:structured-plan"]}],"edges":[],"open_questions":[],"plan_rationale":"bridge payload should become the active plan graph","estimated_complexity":1})";
}

[[nodiscard]] std::string make_structured_execution_payload() {
  return R"({"schema_version":"cognition.reasoning.v1","decision_kind":"DirectResponse","confidence":0.79,"rationale":"bridge execution payload should become the authoritative decision","selected_node_id":null,"tool_intent_hint":null,"clarification_needed":false,"clarification_question":null,"response_outline":{"summary":"bridge-authored direct response summary","key_points":["respond from the bridge payload","skip local execute_action routing"]},"candidate_scores":[{"candidate_name":"direct_response","score":0.79,"rationale":"bridge payload selected direct response"},{"candidate_name":"execute_action","score":0.21,"rationale":"local execution should not win once projection succeeds"}]})";
}

[[nodiscard]] std::string make_invalid_execution_payload() {
  return R"({"schema_version":"cognition.reasoning.v1","decision_kind":"DirectResponse","confidence":0.79,"rationale":"response decisions must not carry executable tool intent","selected_node_id":null,"tool_intent_hint":{"tool_name":"agent.dataset","intent_summary":"this should trigger invariant failure","argument_hints":[],"evidence_refs":[]},"clarification_needed":false,"clarification_question":null,"response_outline":{"summary":"bridge-authored direct response summary","key_points":["this payload should fail closed"]},"candidate_scores":[{"candidate_name":"direct_response","score":0.79,"rationale":"invalid because tool_intent_hint is present"}]})";
}

void test_decide_uses_projected_action_decision_as_authoritative_result() {
  MockCognitionFixture fixture;
  fixture.llm_manager()->set_stage_result(
      "planning",
      MockLLMManager::make_success_result(
          make_structured_planning_payload(),
          "mock.route.planning",
          fixture.options().request_id));
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_success_result(
          make_structured_execution_payload(),
          "mock.route.execution",
          fixture.options().request_id));

  auto engine = fixture.make_engine(CognitionConfig{});

  const auto result = engine->decide(fixture.make_decide_request(true));

  assert_true(!result.result_code.has_value(),
              "valid structured execution payload should keep the decide result successful");
  assert_true(result.action_decision.has_value(),
              "valid structured execution payload should return an authoritative action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::DirectResponse,
              "execution bridge payload should override the local execute_action path");
  assert_true(result.action_decision->response_outline.has_value(),
              "projected action decisions should preserve the response outline");
  assert_equal(std::string("bridge-authored direct response summary"),
               result.action_decision->response_outline->summary,
               "execution bridge payload should become the authoritative response summary");
  assert_true(contains_value(result.diagnostics, "structured_projection.projected_action_decision"),
              "execution bridge success should mark projected_action_decision diagnostics");
  assert_true(!contains_value(result.diagnostics, "structured_projection.local_fallback:execution"),
              "successful execution projection must not fall back to the local reasoner");
}

void test_decide_fails_closed_when_invalid_execution_projection_has_no_fallback() {
  MockCognitionFixture fixture;
  fixture.llm_manager()->set_stage_result(
      "planning",
      MockLLMManager::make_success_result(
          make_structured_planning_payload(),
          "mock.route.planning",
          fixture.options().request_id));
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_success_result(
          make_invalid_execution_payload(),
          "mock.route.execution",
          fixture.options().request_id));

  auto engine = fixture.make_engine(CognitionConfig{});
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(result.result_code.has_value(),
              "invalid execution payload should fail closed when degradation is disabled");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(*result.result_code),
               "invalid execution payload should surface the canonical validation result code");
  assert_true(result.error_info.has_value(),
              "invalid execution payload should emit an explicit ErrorInfo payload");
  assert_true(!result.action_decision.has_value(),
              "fail-closed execution projection must not return a partial action decision");
  assert_true(contains_value(result.diagnostics, "structured_projection.invariant_failed:execution"),
              "invalid execution payloads should surface the invariant_failed diagnostic");
}

}  // namespace

int main() {
  try {
    test_decide_uses_projected_action_decision_as_authoritative_result();
    test_decide_fails_closed_when_invalid_execution_projection_has_no_fallback();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}