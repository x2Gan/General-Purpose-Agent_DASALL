#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "MockCognitionFixture.h"
#include "error/ResultCode.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ICognitionEngine;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::contracts::ResultCode;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
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
      });
  assert_true(engine != nullptr,
              "structured output integration requires a snapshot-backed cognition engine");
  return engine;
}

void test_decide_projects_structured_plan_and_action_on_snapshot_backed_path() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "structured-integration-node",
      .response_text = "bridge-authored direct response summary",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);

  auto engine = make_snapshot_backed_engine("desktop_full", fixture);
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "valid structured payloads should keep the snapshot-backed decide path successful");
  assert_true(result.action_decision.has_value(),
              "valid structured payloads should yield an authoritative action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::DirectResponse,
              "valid structured execution payload should produce a direct response decision");
  assert_true(result.action_decision->response_outline.has_value(),
              "projected action decisions should preserve the bridge response outline");
  assert_equal(std::string{"bridge-authored direct response summary"},
               result.action_decision->response_outline->summary,
               "structured execution payload should become the authoritative response outline");
  assert_true(contains_value(result.diagnostics, "structured_projection.projected_plan_graph"),
              "integration path should mark projected plan graph diagnostics");
  assert_true(
      contains_value(result.diagnostics, "structured_projection.projected_action_decision"),
      "integration path should mark projected action decision diagnostics");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.local_fallback:planning"),
              "valid planning payload should not fall back to the local planner");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.local_fallback:execution"),
              "valid execution payload should not fall back to the local reasoner");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "integration path should dispatch the planning bridge stage");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "integration path should dispatch the execution bridge stage");
}

void test_decide_explicitly_falls_back_when_planning_payload_is_malformed() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "structured-fallback-node",
      .response_text = "fallback path still accepts structured execution output",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::MalformedJson);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);

  auto engine = make_snapshot_backed_engine("edge_balanced", fixture);
  auto request = fixture.make_decide_request(true);

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "malformed planning payload should degrade to explicit fallback when allowed");
  assert_true(result.action_decision.has_value(),
              "planning fallback should still yield a bounded action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::DirectResponse,
              "execution structured payload should remain authoritative after planning fallback");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.schema_violation:planning"),
              "malformed planning payload should surface the schema violation diagnostic");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.local_fallback:planning"),
              "malformed planning payload should record explicit planning fallback");
  assert_true(
      contains_value(result.diagnostics, "structured_projection.projected_action_decision"),
      "execution stage should still project an authoritative action decision after planning fallback");
}

void test_decide_fails_closed_when_execution_projection_is_invalid_without_fallback() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "structured-failfast-node",
      .response_text = "this response should never be emitted",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ProjectionInvalidToolIntentOnDirectResponse);

  auto engine = make_snapshot_backed_engine("desktop_full", fixture);
  auto request = fixture.make_decide_request(true);
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->decide(request);

  assert_true(result.result_code.has_value(),
              "invalid execution projection should fail closed when fallback is disabled");
  assert_equal(static_cast<int>(ResultCode::ValidationFieldMissing),
               static_cast<int>(*result.result_code),
               "invalid execution projection should surface the canonical validation code");
  assert_true(result.error_info.has_value(),
              "fail-closed execution projection should return structured error info");
  assert_true(!result.action_decision.has_value(),
              "fail-closed execution projection must not leak a partial action decision");
  assert_true(contains_value(result.diagnostics, "structured_projection.projected_plan_graph"),
              "planning stage should remain projected before execution fail-fast");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.invariant_failed:execution"),
              "invalid execution payload should surface invariant failure diagnostics");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.local_fallback:execution"),
              "execution fail-fast path must not silently fall back when disabled");
}

}  // namespace

int main() {
  try {
    test_decide_projects_structured_plan_and_action_on_snapshot_backed_path();
    test_decide_explicitly_falls_back_when_planning_payload_is_malformed();
    test_decide_fails_closed_when_execution_projection_is_invalid_without_fallback();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}