#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "CognitionConfig.h"
#include "decision/ActionDecision.h"
#include "support/TestAssertions.h"
#include "MockCognitionFixture.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredPerceptionPayloadScenario;
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

void test_decide_returns_clarification_before_planning_when_perception_paths_disagree() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "perception-disagreement-node",
  });
  fixture.stage_structured_perception_result(StructuredPerceptionPayloadScenario::ValidPlan);

  auto engine = fixture.make_engine(CognitionConfig{});
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "perception disagreement should resolve to a safe clarification result instead of failing closed");
  assert_true(result.action_decision.has_value(),
              "perception disagreement should still yield a bounded action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::AskClarification,
              "perception disagreement must short-circuit to AskClarification before planning");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.projected_perception_result"),
              "successful perception bridge projection should be recorded explicitly");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.source:perception:llm_bridge"),
              "perception disagreement should keep the llm bridge as the authoritative perception source");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.perception_conflict"),
              "perception disagreement should surface an explicit conflict diagnostic");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "perception"),
              "perception disagreement should still dispatch the perception llm stage");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "perception disagreement must stop before planning bridge dispatch");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "perception disagreement must stop before execution bridge dispatch");
}

void test_decide_falls_back_to_local_perception_when_invariants_fail_and_degradation_is_allowed() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "perception-fallback-node",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ProjectionInvalidEmptyEntityName);

  auto engine = fixture.make_engine(CognitionConfig{});
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = true;

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "invalid perception payloads should degrade to the local rule path when degradation is allowed");
  assert_true(result.action_decision.has_value(),
              "perception fallback should still yield a bounded decision through the local pipeline");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.invariant_failed:perception"),
              "invalid perception payloads should surface the perception invariant diagnostic");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.local_fallback:perception"),
              "perception invariant failures should record explicit local fallback ownership");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "local perception fallback should still continue into planning");
}

void test_decide_fails_closed_when_perception_invariants_fail_without_fallback() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "perception-failfast-node",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ProjectionInvalidEmptyEntityName);

  auto engine = fixture.make_engine(CognitionConfig{});
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(result.result_code.has_value(),
              "invalid perception payloads must fail closed when degradation is disabled");
  assert_true(!result.action_decision.has_value(),
              "fail-closed perception validation must not leak a partial action decision");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.invariant_failed:perception"),
              "fail-closed perception validation should surface the invariant diagnostic");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.local_fallback:perception"),
              "fail-closed perception validation must not claim a local fallback source");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "fail-closed perception validation must stop before planning bridge dispatch");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "fail-closed perception validation must stop before execution bridge dispatch");
}

}  // namespace

int main() {
  try {
    test_decide_returns_clarification_before_planning_when_perception_paths_disagree();
    test_decide_falls_back_to_local_perception_when_invariants_fail_and_degradation_is_allowed();
    test_decide_fails_closed_when_perception_invariants_fail_without_fallback();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}