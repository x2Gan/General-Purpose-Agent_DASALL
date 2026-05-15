#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "MockCognitionFixture.h"
#include "MockLLMManager.h"
#include "../../../llm/include/route/ModelSelectionHint.h"
#include "support/TestAssertions.h"
#include "validation/InputBoundaryValidator.h"
#include "validation/StructuredPayloadView.h"

namespace {

using dasall::cognition::validation::InputBoundaryValidator;
using dasall::cognition::validation::StructuredPayloadView;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::ModelSelectionHint;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] LLMGenerateRequest make_generate_request(
  std::string stage = "planning",
  std::string task_type = "plan_graph") {
  LLMGenerateRequest request;
  request.stage = std::move(stage);
  request.task_type = std::move(task_type);
  request.request.request_id = std::string{"req-cognition-fixture"};
  request.request.model_route = std::string{"mock.route."} + request.stage;
  request.request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.request.messages = std::vector<std::string>{
    request.stage == "planning" ? std::string{"produce a plan graph"}
                  : std::string{"produce an action decision"},
  };
  request.request.created_at = 1712746800000LL;
  request.request.max_output_tokens = 256U;
  request.selection_hint = std::make_shared<ModelSelectionHint>(ModelSelectionHint{
    .stage = request.stage,
    .task_type = request.task_type,
      .complexity_tier = "standard",
      .latency_sla_tier = "interactive",
      .budget_tier = "balanced",
    .requires_tools = request.stage == "execution",
      .requires_reasoning = true,
      .prefers_visible_reasoning = false,
      .estimated_input_tokens = 128U,
      .target_output_tokens = 256U,
      .previous_route_failures = 0U,
  });
  return request;
}

void test_mock_llm_manager_records_stage_requests_and_returns_structured_results() {
  MockLLMManager manager;
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
    .selected_node_id = "bridge-plan-node",
  });
  manager.set_structured_stage_payload(
    "planning",
    fixture.make_structured_planning_payload(StructuredPlanningPayloadScenario::Valid),
    std::string{"req-cognition-fixture"});

  const auto result = manager.generate(make_generate_request());

  assert_equal(1, manager.generate_call_count(),
               "mock llm manager should record generate call count");
  assert_true(manager.last_request().has_value(),
              "mock llm manager should retain the last request");
  assert_equal(std::string{"planning"}, manager.last_request()->stage,
               "mock llm manager should preserve the stage key");
  assert_equal(std::string{"plan_graph"}, manager.last_request()->task_type,
               "mock llm manager should preserve the task type");
  assert_true(manager.generate_requests().size() == 1U,
              "mock llm manager should retain the generate request sequence");
  assert_equal(std::string{"planning"}, manager.generate_requests().front().stage,
               "recorded request sequence should preserve the stage key");
  assert_true(manager.last_request()->selection_hint != nullptr,
              "mock llm manager should preserve the routed selection hint");
  assert_true(result.has_consistent_values(),
              "scripted mock llm result should satisfy the llm manager invariants");
  assert_true(result.response.has_value() && result.response->content_payload.has_value(),
              "scripted mock llm result should expose a normalized content payload");
  assert_equal(std::string{"mock.route.planning"}, result.resolved_route,
               "structured stage helper should derive the default planning route");
  assert_true(result.response->content_payload->find("\"schema_version\":\"cognition.plan.v1\"") !=
                  std::string::npos,
              "structured stage helper should stage a planning schema payload");
}

void test_mock_cognition_fixture_generates_valid_runtime_requests() {
  MockCognitionFixture fixture;

  const auto decide_request = fixture.make_decide_request(true);
  const auto reflection_request = fixture.make_reflection_request();
  const auto response_request = fixture.make_response_request();

  const auto decide_validation =
      InputBoundaryValidator::validate_decide_request(decide_request);
  const auto reflection_validation =
      InputBoundaryValidator::validate_reflection_request(reflection_request);
  const auto response_validation =
      InputBoundaryValidator::validate_response_request(response_request);

  assert_equal(std::string{"runtime.agent_orchestrator"}, decide_request.caller_domain,
               "mock cognition fixture should default to the runtime caller seam");
  assert_true(decide_validation.ok(),
              "mock cognition fixture should produce a valid decide request");
  assert_true(reflection_validation.ok(),
              "mock cognition fixture should produce a valid reflection request");
  assert_true(response_validation.ok(),
              "mock cognition fixture should produce a valid response request");
  assert_true(fixture.llm_manager() != nullptr,
              "mock cognition fixture should expose a reusable llm manager double");
}

  void test_mock_cognition_fixture_builds_and_stages_structured_payload_scenarios() {
    MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .selected_node_id = "bridge-plan-node",
      .response_text = "bridge-authored direct response summary",
    });

    const auto valid_planning_payload =
      fixture.make_structured_planning_payload(StructuredPlanningPayloadScenario::Valid);
    const auto malformed_planning_payload = fixture.make_structured_planning_payload(
      StructuredPlanningPayloadScenario::MalformedJson);
    const auto schema_invalid_planning_payload = fixture.make_structured_planning_payload(
      StructuredPlanningPayloadScenario::SchemaInvalidActionKindHint);
    const auto projection_invalid_planning_payload = fixture.make_structured_planning_payload(
      StructuredPlanningPayloadScenario::ProjectionInvalidDuplicateNode);
    const auto valid_execution_payload = fixture.make_structured_execution_payload(
      StructuredExecutionPayloadScenario::ValidDirectResponse);
    const auto malformed_execution_payload = fixture.make_structured_execution_payload(
      StructuredExecutionPayloadScenario::MalformedJson);
    const auto schema_invalid_execution_payload = fixture.make_structured_execution_payload(
      StructuredExecutionPayloadScenario::SchemaInvalidDecisionKind);
    const auto projection_invalid_execution_payload = fixture.make_structured_execution_payload(
      StructuredExecutionPayloadScenario::ProjectionInvalidToolIntentOnDirectResponse);

    assert_true(StructuredPayloadView::parse_structured_payload(valid_planning_payload).has_value(),
          "valid structured planning payload should parse as JSON");
    assert_true(!StructuredPayloadView::parse_structured_payload(malformed_planning_payload)
             .has_value(),
          "malformed structured planning payload should fail parsing");
    assert_true(
      StructuredPayloadView::parse_structured_payload(schema_invalid_planning_payload).has_value(),
      "schema-invalid planning payload should remain parseable for validator coverage");
    assert_true(StructuredPayloadView::parse_structured_payload(
            projection_invalid_planning_payload)
            .has_value(),
          "projection-invalid planning payload should remain parseable for projector coverage");
    assert_true(StructuredPayloadView::parse_structured_payload(valid_execution_payload).has_value(),
          "valid structured execution payload should parse as JSON");
    assert_true(!StructuredPayloadView::parse_structured_payload(malformed_execution_payload)
             .has_value(),
          "malformed structured execution payload should fail parsing");
    assert_true(
      StructuredPayloadView::parse_structured_payload(schema_invalid_execution_payload).has_value(),
      "schema-invalid execution payload should remain parseable for validator coverage");
    assert_true(StructuredPayloadView::parse_structured_payload(
            projection_invalid_execution_payload)
            .has_value(),
          "projection-invalid execution payload should remain parseable for projector coverage");

    fixture.stage_structured_planning_result(
      StructuredPlanningPayloadScenario::ProjectionInvalidDuplicateNode);
    fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::SchemaInvalidDecisionKind);

    const auto planning_result = fixture.llm_manager()->generate(
      make_generate_request("planning", "plan_graph"));
    const auto execution_result = fixture.llm_manager()->generate(
      make_generate_request("execution", "action_decision"));

    assert_true(planning_result.response.has_value() &&
            planning_result.response->content_payload.has_value(),
          "fixture should stage structured planning results onto its shared llm manager");
    assert_true(execution_result.response.has_value() &&
            execution_result.response->content_payload.has_value(),
          "fixture should stage structured execution results onto its shared llm manager");
    assert_equal(std::string{"mock.route.planning"}, planning_result.resolved_route,
           "fixture planning helper should not require tests to repeat the route mapping");
    assert_equal(std::string{"mock.route.execution"}, execution_result.resolved_route,
           "fixture execution helper should not require tests to repeat the route mapping");
    assert_true(planning_result.response->content_payload->find("\"node_id\":\"bridge-plan-node\"") !=
            std::string::npos,
          "fixture planning helper should project the configured selected node id");
    assert_true(execution_result.response->content_payload->find("\"decision_kind\":\"LaunchAction\"") !=
            std::string::npos,
          "fixture execution helper should expose schema-invalid scenarios without hand-written payloads");
  }

void test_mock_cognition_fixture_exposes_shared_ports_and_response_result() {
  MockCognitionFixture fixture;

  const auto engine = fixture.make_engine();
  const auto builder = fixture.make_response_builder();
  const auto response_result = fixture.make_response_result();

  assert_true(engine != nullptr,
              "mock cognition fixture should materialize a shared cognition engine");
  assert_true(builder != nullptr,
              "mock cognition fixture should materialize a shared response builder");
  assert_true(response_result.agent_result.has_value(),
              "mock cognition fixture should build a terminal agent result envelope");
  assert_true(response_result.agent_result->task_completed.value_or(false),
              "fixture success response result should mark task_completed=true");
  assert_true(!response_result.fallback_used,
              "fixture success response result should not mark template fallback");
}

}  // namespace

int main() {
  try {
    test_mock_llm_manager_records_stage_requests_and_returns_structured_results();
    test_mock_cognition_fixture_generates_valid_runtime_requests();
    test_mock_cognition_fixture_builds_and_stages_structured_payload_scenarios();
    test_mock_cognition_fixture_exposes_shared_ports_and_response_result();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
