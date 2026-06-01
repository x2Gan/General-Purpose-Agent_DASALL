#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ICognitionEngine.h"
#include "checkpoint/ReflectionDecision.h"
#include "support/TestAssertions.h"
#include "MockCognitionFixture.h"

namespace {

using dasall::contracts::ReflectionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::MockLLMManager;
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

[[nodiscard]] const dasall::llm::LLMGenerateRequest* nth_reflection_request(
    const std::vector<dasall::llm::LLMGenerateRequest>& requests,
    const std::size_t ordinal) {
  std::size_t seen = 0U;
  for (const auto& request : requests) {
    if (request.stage != "reflection") {
      continue;
    }
    if (seen == ordinal) {
      return &request;
    }
    ++seen;
  }

  return nullptr;
}

[[nodiscard]] dasall::contracts::Observation make_reasoning_failure_observation(
    const MockCognitionFixture& fixture) {
  auto observation = fixture.make_observation(
      false, "reasoning selected stale evidence and produced a mismatched recovery suggestion");
  observation.error = dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = false,
      .safe_to_replan = true,
      .details = {.code = 5001,
                  .message =
                      "reasoning selected stale evidence and produced a mismatched recovery suggestion",
                  .stage = "reasoning"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-reflection-self-refine"},
  };
  return observation;
}

[[nodiscard]] dasall::cognition::plan::PlanGraph make_active_plan() {
  dasall::cognition::plan::PlanGraph active_plan;
  active_plan.plan_id = "plan-reflection-self-refine";
  active_plan.revision = 2U;
  active_plan.nodes = {
      dasall::cognition::plan::PlanNode{
          .node_id = "plan-node:reflection-self-refine",
          .objective = "repair the reasoning path before runtime considers recovery",
          .success_signal = "reflection emits a stable replan suggestion",
          .action_kind_hint = "reasoning",
          .depends_on = {},
          .evidence_refs = {"tests:reflection-self-refine"},
      },
  };
  active_plan.plan_rationale = "self-refine should preserve the active node anchor";
  active_plan.estimated_complexity = 1U;
  return active_plan;
}

[[nodiscard]] std::string make_reflection_payload(
    const MockCognitionFixtureOptions& options,
    const std::string& decision_kind,
    const std::string& rationale,
    const std::string& hint_ref) {
  return std::string{"{"}
      + "\"schema_version\":\"cognition.reflection.v1\","
      + "\"request_id\":\"" + options.request_id + "\","
      + "\"decision_kind\":\"" + decision_kind + "\","
      + "\"rationale\":\"" + rationale + "\","
      + "\"goal_id\":\"" + options.goal_id + "\","
      + "\"confidence\":0.89,"
      + "\"relevant_observation_refs\":[\"obs-reflection-self-refine\"],"
      + "\"hint_ref\":\"" + hint_ref + "\","
      + "\"created_at\":1712746800000,"
      + "\"tags\":[\"cognition\",\"reflection\"]}"
      ;
}

void test_reflection_self_refine_runs_exactly_one_additional_bridge_round() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-reflection-self-refine-single-round",
      .trace_id = "trace-reflection-self-refine-single-round",
      .profile_id = "desktop_full",
      .goal_id = "goal-reflection-self-refine-single-round",
      .observation_id = "obs-reflection-self-refine",
  });

  auto first_payload = make_reflection_payload(
      fixture.options(),
      "Continue",
      "first reflection pass stayed on the stale reasoning branch",
      "hint:reflection:continue");
  auto second_payload = make_reflection_payload(
      fixture.options(),
      "Replan",
      "self refine concluded that the stale reasoning branch must be replanned",
      "hint:reflection:replan");

  int reflection_round = 0;
  fixture.llm_manager()->set_generate_handler(
      [&reflection_round,
       first_payload = std::move(first_payload),
       second_payload = std::move(second_payload)](
          const dasall::llm::LLMGenerateRequest& request) mutable {
        if (request.stage != "reflection") {
          return MockLLMManager::make_success_result(
              std::string{"mock-content-for-"} + request.stage,
              std::string{"mock.route."} + request.stage,
              request.request.request_id);
        }

        ++reflection_round;
        if (reflection_round == 1) {
          return MockLLMManager::make_structured_stage_result(
              "reflection", first_payload, request.request.request_id);
        }

        return MockLLMManager::make_structured_stage_result(
            "reflection", second_payload, request.request.request_id);
      });

  auto engine = fixture.make_engine();
  auto request = fixture.make_reflection_request(make_reasoning_failure_observation(fixture));
  request.active_plan = make_active_plan();
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->reflect(request);

  assert_true(!result.result_code.has_value(),
              "self-refine happy path should stay on reflection success");
  assert_true(result.reflection_decision.has_value(),
              "self-refine happy path should still yield a reflection decision");
  assert_true(result.reflection_decision->decision_kind.has_value() &&
                  *result.reflection_decision->decision_kind == ReflectionDecisionKind::Replan,
              "second reflection round should be able to replace the first decision");
  assert_true(result.reflection_decision->rationale.has_value() &&
                  result.reflection_decision->rationale->find(
                      "self refine concluded") != std::string::npos,
              "final reflection rationale should come from the self-refined bridge payload");
  assert_true(contains_value(result.diagnostics,
                             "reflection_pipeline.self_refine.started"),
              "reflection pipeline should record the self-refine start diagnostic");
  assert_true(contains_value(result.diagnostics,
                             "reflection_pipeline.self_refine.completed"),
              "reflection pipeline should record the self-refine completion diagnostic");

  const auto* first_request =
      nth_reflection_request(fixture.llm_manager()->generate_requests(), 0U);
  const auto* second_request =
      nth_reflection_request(fixture.llm_manager()->generate_requests(), 1U);
  assert_true(first_request != nullptr && second_request != nullptr,
              "self-refine should dispatch exactly two reflection bridge requests");
  assert_true(first_request->task_type == "failure_analysis",
              "first reflection bridge request must stay on failure_analysis");
  assert_true(second_request->task_type == "replan_advice",
              "second reflection bridge request must switch to replan_advice");
  assert_true(first_request->request.max_output_tokens.has_value() &&
                  second_request->request.max_output_tokens.has_value() &&
                  *second_request->request.max_output_tokens <
                      *first_request->request.max_output_tokens,
              "self-refine round should run under a tighter output token cap");
  assert_true(first_request->request.timeout_ms.has_value() &&
                  second_request->request.timeout_ms.has_value() &&
                  *second_request->request.timeout_ms < *first_request->request.timeout_ms,
              "self-refine round should run under a tighter deadline cap");
}

}  // namespace

int main() {
  try {
    test_reflection_self_refine_runs_exactly_one_additional_bridge_round();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}