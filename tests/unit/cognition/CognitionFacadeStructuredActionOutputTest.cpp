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
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
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

void test_decide_uses_projected_action_decision_as_authoritative_result() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "bridge-plan-node",
      .response_text = "bridge-authored direct response summary",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);

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
  assert_true(contains_value(result.diagnostics, "structured_projection.enabled:execution"),
              "execution bridge path should stamp structured projection enabled diagnostics");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.schema_version:execution:cognition.reasoning.v1"),
              "execution bridge path should expose the structured schema version");
  assert_true(contains_value(result.diagnostics, "structured_projection.projected_action_decision"),
              "execution bridge success should mark projected_action_decision diagnostics");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.source:execution:llm_bridge"),
              "execution projection success should record the authoritative bridge source");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.projected_candidate_count:execution:2"),
              "execution projection success should record the projected candidate count");
  assert_true(!contains_value(result.diagnostics, "structured_projection.local_fallback:execution"),
              "successful execution projection must not fall back to the local reasoner");
}

void test_decide_fails_closed_when_invalid_execution_projection_has_no_fallback() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
    .selected_node_id = "bridge-plan-node",
    .response_text = "bridge-authored direct response summary",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
    StructuredExecutionPayloadScenario::ProjectionInvalidToolIntentOnDirectResponse);

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
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.failure_code:execution:invariant"),
              "invalid execution payloads should expose the invariant failure code");
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