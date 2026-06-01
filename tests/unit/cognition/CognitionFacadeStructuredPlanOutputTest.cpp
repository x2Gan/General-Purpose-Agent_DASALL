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
using dasall::tests::mocks::StructuredPerceptionPayloadScenario;
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
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
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
  assert_true(contains_value(result.diagnostics, "structured_projection.enabled:planning"),
              "planning bridge path should stamp structured projection enabled diagnostics");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.schema_version:planning:cognition.plan.v1"),
              "planning bridge path should expose the structured schema version");
  assert_true(contains_value(result.diagnostics, "structured_projection.projected_plan_graph"),
              "planning bridge success should mark projected_plan_graph diagnostics");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.source:planning:llm_bridge"),
              "planning projection success should record the authoritative bridge source");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.projected_node_count:planning:1"),
              "planning projection success should record the projected node count");
  assert_true(!contains_value(result.diagnostics, "structured_projection.local_fallback:planning"),
              "successful planning projection must not fall back to the local planner");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.llm_bridge_degraded:execution"),
              "execution bridge failure should remain visible when local reasoner fallback is used");
  assert_true(contains_value(result.diagnostics, "structured_projection.local_fallback:execution"),
              "execution bridge failure should record explicit local fallback ownership");
}

void test_decide_falls_back_to_local_planner_only_on_explicit_planning_projection_failure() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
    .selected_node_id = "invalid-bridge-node",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
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
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.failure_code:planning:schema"),
              "invalid planning bridge payloads should expose the schema failure code");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.source:planning:local_fallback"),
              "planning fallback should record the local fallback source");
  assert_true(contains_value(result.diagnostics, "structured_projection.local_fallback:planning"),
              "local planner usage must be explicit when planning projection fails");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.degraded"),
              "planning fallback should stamp the pipeline degradation diagnostic");
}

void test_decide_fails_closed_when_planning_depends_on_is_semantically_invalid() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "bridge-plan-node",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
  fixture.llm_manager()->set_structured_stage_payload(
      "planning",
      std::string{"{"}
          + "\"schema_version\":\"cognition.plan.v1\","
          + "\"plan_id\":\"plan-invalid-dependency\","
          + "\"revision\":1,"
          + "\"nodes\":[{"
          + "\"node_id\":\"bridge-plan-node\","
          + "\"objective\":\"collect governed evidence\","
          + "\"success_signal\":\"evidence_collected\","
          + "\"action_kind_hint\":\"tool_action\","
          + "\"depends_on\":[\"missing-node\"],"
          + "\"evidence_refs\":[]}],"
          + "\"edges\":[],"
          + "\"open_questions\":[],"
          + "\"plan_rationale\":\"depends_on must reference a projected node\","
          + "\"estimated_complexity\":1}" );

  auto engine = fixture.make_engine(CognitionConfig{});
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(result.result_code.has_value(),
              "invalid depends_on semantics should fail closed when degradation is disabled");
  assert_true(!result.action_decision.has_value(),
              "planning invariant failures must not leak a partial action decision");
  assert_true(contains_value(result.diagnostics, "structured_projection.invariant_failed:planning"),
              "invalid depends_on semantics should surface the planning invariant diagnostic");
  assert_true(!contains_value(result.diagnostics, "structured_projection.local_fallback:planning"),
              "planning invariant failures must not silently fall back when degradation is disabled");
}

void test_decide_fails_closed_when_execution_bridge_provider_failure_has_no_fallback() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "bridge-plan-node",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "execution bridge intentionally unavailable to verify fail-closed behavior",
          LLMFailureCategory::ProviderProtocol,
          "mock.route.execution",
          fixture.options().request_id));

  auto engine = fixture.make_engine(CognitionConfig{});
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(result.result_code.has_value(),
              "execution bridge provider failures must fail closed when fallback is disabled");
  assert_true(!result.action_decision.has_value(),
              "provider failures without fallback must not leak a partial action decision");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.llm_bridge_failed:execution"),
              "provider failures without fallback should surface an explicit bridge failure diagnostic");
  assert_true(!contains_value(result.diagnostics, "structured_projection.local_fallback:execution"),
              "provider failures without fallback must not claim a local fallback source");
}

}  // namespace

int main() {
  try {
    test_decide_uses_projected_plan_graph_as_reasoner_input();
    test_decide_falls_back_to_local_planner_only_on_explicit_planning_projection_failure();
    test_decide_fails_closed_when_planning_depends_on_is_semantically_invalid();
    test_decide_fails_closed_when_execution_bridge_provider_failure_has_no_fallback();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}