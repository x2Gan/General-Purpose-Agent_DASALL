#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "MockCognitionFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ICognitionEngine;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPerceptionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::runtime_fixture::make_true_integration_policy_snapshot;
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

[[nodiscard]] std::unique_ptr<ICognitionEngine> make_snapshot_backed_engine(
    const std::string& profile_id,
    const MockCognitionFixture& fixture) {
  const auto snapshot = make_true_integration_policy_snapshot(profile_id);
  auto engine = dasall::cognition::create_cognition_engine(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
          .llm_manager = fixture.llm_manager(),
          .policy_snapshot = snapshot,
      });
  assert_true(engine != nullptr,
              "perception structured output integration requires a snapshot-backed cognition engine");
  return engine;
}

void test_decide_projects_structured_perception_before_planning_on_snapshot_backed_path() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "structured-perception-node",
      .response_text = "bridge-authored direct response summary",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);

  auto engine = make_snapshot_backed_engine("desktop_full", fixture);
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "valid structured perception, planning and execution payloads should keep the snapshot-backed decide path successful");
  assert_true(result.action_decision.has_value(),
              "valid structured perception should still yield an authoritative action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::DirectResponse,
              "valid structured execution payload should remain authoritative after perception projection");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.projected_perception_result"),
              "integration path should mark projected perception diagnostics");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.source:perception:llm_bridge"),
              "integration path should record llm_bridge as the perception authority");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.local_fallback:perception"),
              "valid perception payload should not fall back to the local rule path");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "perception"),
              "integration path should dispatch the perception bridge stage");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "integration path should dispatch the planning bridge stage after perception succeeds");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "integration path should dispatch the execution bridge stage after perception succeeds");
}

void test_decide_returns_ask_clarification_before_planning_when_perception_requires_more_evidence() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "structured-perception-clarification-node",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidClarification);

  auto engine = make_snapshot_backed_engine("desktop_full", fixture);
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "clarification-oriented perception payloads should return a bounded clarification result");
  assert_true(result.action_decision.has_value(),
              "clarification-oriented perception payloads should still yield an action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::AskClarification,
              "perception clarification payloads must short-circuit to AskClarification before planning");
  assert_equal(std::string("Which concrete target should cognition confirm before planning continues?"),
               result.action_decision->clarification_question.value_or(std::string{}),
               "perception clarification payload should preserve the authoritative clarification question");
  assert_true(contains_value(result.diagnostics,
                             "decision_pipeline.perception_clarification_required"),
              "perception clarification path should surface an explicit clarification diagnostic");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "perception"),
              "clarification path should still dispatch the perception bridge stage");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "clarification path must stop before planning bridge dispatch");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "clarification path must stop before execution bridge dispatch");
}

}  // namespace

int main() {
  try {
    test_decide_projects_structured_perception_before_planning_on_snapshot_backed_path();
    test_decide_returns_ask_clarification_before_planning_when_perception_requires_more_evidence();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}