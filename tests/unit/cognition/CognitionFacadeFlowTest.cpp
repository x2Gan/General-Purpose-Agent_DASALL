#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "agent/AgentResult.h"
#include "checkpoint/ReflectionDecision.h"
#include "decision/ActionDecision.h"
#include "support/TestAssertions.h"
#include "tests/mocks/include/MockCognitionFixture.h"

namespace {

using dasall::cognition::decision::ActionDecisionKind;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::ReflectionDecisionKind;
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

[[nodiscard]] bool contains_stage(
    const std::vector<dasall::llm::LLMGenerateRequest>& requests,
    const std::string& expected_stage) {
  for (const auto& request : requests) {
    if (request.stage == expected_stage) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] dasall::cognition::plan::PlanGraph make_reflection_active_plan() {
  dasall::cognition::plan::PlanGraph active_plan;
  active_plan.plan_id = "plan-cognition-fixture";
  active_plan.revision = 1U;
  active_plan.nodes = {
      dasall::cognition::plan::PlanNode{
          .node_id = "plan-node:fixture",
          .objective = "query runtime-visible data through tool governance",
          .success_signal = "tool projection should satisfy the current step",
          .action_kind_hint = "tool_execution",
          .depends_on = {},
          .evidence_refs = {"tests:mock-cognition-fixture"},
      },
  };
  active_plan.plan_rationale = "fixture reflection path should preserve active plan";
  active_plan.estimated_complexity = 1U;
  return active_plan;
}

[[nodiscard]] std::string make_structured_response_payload() {
  return std::string{"{"}
      + "\"schema_version\":\"cognition.response.v1\","
      + "\"response_mode\":\"llm_bridge\","
      + "\"summary_text\":\"llm bridge composed the final response\","
      + "\"structured_sections\":[\"summary\"],"
      + "\"omitted_details\":[],"
      + "\"fallback_used\":false}"
      ;
}

void test_cognition_facade_orchestrates_decide_reflect_and_response_flow() {
  MockCognitionFixture fixture;
  fixture.llm_manager()->set_structured_stage_payload(
      "response",
      make_structured_response_payload(),
      std::string{"req-cognition-fixture"});
  auto engine = fixture.make_engine();
  auto builder = fixture.make_response_builder();

  const auto decision_result = engine->decide(fixture.make_decide_request(true));

  assert_true(!decision_result.result_code.has_value(),
              "valid decide flow should not emit a result code");
  assert_true(decision_result.action_decision.has_value(),
              "valid decide flow should synthesize an action decision");
  assert_equal(static_cast<int>(ActionDecisionKind::ExecuteAction),
               static_cast<int>(decision_result.action_decision->decision_kind),
               "facade should route actionable requests to execute_action");
  assert_true(decision_result.belief_update_hint.has_value(),
              "valid decide flow should project a belief update hint");
  assert_true(!decision_result.belief_update_hint->confirmed_facts_delta.empty(),
              "belief update hint should confirm at least one fact after decide");
  assert_true(decision_result.context_sufficiency.context_sufficient,
              "clear actionable requests should retain sufficient context");
  assert_true(contains_value(decision_result.diagnostics, "decision_pipeline.completed"),
              "decide flow should stamp a pipeline completion diagnostic");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "decide flow should invoke the bridge through the planning canonical stage");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "decide flow should invoke the bridge through the execution canonical stage");

  const auto reflection_result =
      engine->reflect(fixture.make_reflection_request(fixture.make_observation(true)));

  assert_true(!reflection_result.result_code.has_value(),
              "valid reflection flow should not emit a result code");
  assert_true(reflection_result.reflection_decision.has_value(),
              "valid reflection flow should return a reflection decision");
  assert_true(reflection_result.reflection_decision->decision_kind.has_value(),
              "reflection decision should carry a concrete decision kind");
  assert_equal(static_cast<int>(ReflectionDecisionKind::Continue),
               static_cast<int>(*reflection_result.reflection_decision->decision_kind),
               "successful observations should keep the reflection decision on continue");
  assert_true(reflection_result.belief_update_hint.has_value(),
              "reflection flow should synthesize a belief update hint");
  assert_true(contains_value(reflection_result.diagnostics, "reflection_pipeline.completed"),
              "reflection flow should stamp a pipeline completion diagnostic");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "reflection"),
              "reflection flow should invoke the bridge through the reflection canonical stage");

  const auto response_result = builder->build(
      fixture.make_response_request(*decision_result.action_decision, fixture.make_observation(true)));

  assert_true(!response_result.result_code.has_value(),
              "response builder should accept the facade-composed terminal flow");
  assert_true(response_result.agent_result.has_value(),
              "response builder should produce an agent result for a complete flow");
  assert_true(response_result.agent_result->status.has_value(),
              "response builder should stamp the terminal agent status");
  assert_equal(static_cast<int>(AgentResultStatus::Completed),
               static_cast<int>(*response_result.agent_result->status),
               "response builder should stay on the bridge-backed path when observation payload exists");
  assert_true(!response_result.fallback_used,
              "response builder should avoid template fallback in the happy path");
  assert_true(contains_value(response_result.diagnostics, "response_mode:llm_bridge"),
              "response builder should mark the bridge-backed response mode");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "response"),
              "response builder should invoke the bridge through the response canonical stage");
  assert_true(response_result.agent_result->response_text.has_value() &&
                  response_result.agent_result->response_text->find("llm bridge composed") !=
                      std::string::npos,
              "response builder should consume the bridge content instead of observation projection");
}

void test_cognition_facade_reflection_consumes_active_plan() {
  MockCognitionFixture fixture;
  auto engine = fixture.make_engine();

  auto failed_observation = fixture.make_observation(
      false, "dataset request timed out while collecting the governed evidence");
  failed_observation.error = dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Tool,
      .retryable = true,
      .safe_to_replan = true,
      .details = {.code = 408,
                  .message = "dataset request timed out while collecting the governed evidence",
                  .stage = "tool_execution"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-cognition-fixture"},
  };

  auto request = fixture.make_reflection_request(failed_observation);
  request.active_plan = make_reflection_active_plan();

  const auto reflection_result = engine->reflect(request);

  assert_true(!reflection_result.result_code.has_value(),
              "reflection with an active plan should stay on the happy owner path");
  assert_true(reflection_result.reflection_decision.has_value(),
              "reflection with an active plan should still produce a reflection decision");
  assert_true(reflection_result.reflection_decision->decision_kind.has_value() &&
                  *reflection_result.reflection_decision->decision_kind ==
                      ReflectionDecisionKind::RetryStep,
              "retryable local failures with an active plan should keep the retry_step decision");
  assert_true(reflection_result.reflection_decision->rationale.has_value() &&
                  reflection_result.reflection_decision->rationale->find(
                      "active_node=plan-node:fixture") != std::string::npos,
              "reflection rationale should retain the active plan node id once the public request carries the active plan");
}

}  // namespace

int main() {
  try {
    test_cognition_facade_orchestrates_decide_reflect_and_response_flow();
    test_cognition_facade_reflection_consumes_active_plan();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
