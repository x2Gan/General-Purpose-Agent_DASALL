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
using dasall::llm::LLMFailureCategory;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
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

void test_decide_uses_projected_plan_graph_as_reasoner_input() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "bridge-plan-node",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "execution bridge intentionally unavailable to force local reasoner fallback",
          LLMFailureCategory::ProviderProtocol,
          "mock.route.execution",
          fixture.options().request_id));

  auto engine = fixture.make_engine(CognitionConfig{});

  const auto result = engine->decide(fixture.make_decide_request(true));

  assert_true(!result.result_code.has_value(),
              "valid structured planning payload should keep the decide result successful");
  assert_true(result.action_decision.has_value(),
              "projected planning payload should still yield an action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::ExecuteAction,
              "local reasoner fallback should still produce an execute_action decision");
  assert_true(result.action_decision->selected_node_id.has_value(),
              "local reasoner should consume the projected plan node id");
  assert_equal(std::string("bridge-plan-node"), *result.action_decision->selected_node_id,
               "structured planning payload must become the active plan graph for reasoning");
  assert_true(contains_value(result.diagnostics, "structured_projection.projected_plan_graph"),
              "planning bridge success should mark projected_plan_graph diagnostics");
  assert_true(!contains_value(result.diagnostics, "structured_projection.local_fallback:planning"),
              "successful planning projection must not fall back to the local planner");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.llm_bridge_degraded:execution"),
              "execution bridge failure should remain visible when local reasoner fallback is used");
}

void test_decide_falls_back_to_local_planner_only_on_explicit_planning_projection_failure() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
    .selected_node_id = "invalid-bridge-node",
  });
  fixture.stage_structured_planning_result(
    StructuredPlanningPayloadScenario::SchemaInvalidActionKindHint);
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "execution bridge intentionally unavailable to keep the test on the local fallback path",
          LLMFailureCategory::ProviderProtocol,
          "mock.route.execution",
          fixture.options().request_id));

  auto engine = fixture.make_engine(CognitionConfig{});

  const auto result = engine->decide(fixture.make_decide_request(true));

  assert_true(!result.result_code.has_value(),
              "explicit planning fallback should keep the decide flow fail-open when degradation is allowed");
  assert_true(result.action_decision.has_value(),
              "explicit planning fallback should still yield a bounded action decision");
  assert_true(contains_value(result.diagnostics, "structured_projection.schema_violation:planning"),
              "invalid planning bridge payloads should surface the schema_violation diagnostic");
  assert_true(contains_value(result.diagnostics, "structured_projection.local_fallback:planning"),
              "local planner usage must be explicit when planning projection fails");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.degraded"),
              "planning fallback should stamp the pipeline degradation diagnostic");
}

}  // namespace

int main() {
  try {
    test_decide_uses_projected_plan_graph_as_reasoner_input();
    test_decide_falls_back_to_local_planner_only_on_explicit_planning_projection_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}