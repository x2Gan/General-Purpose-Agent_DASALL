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

[[nodiscard]] std::size_t count_reflection_requests(
    const std::vector<dasall::llm::LLMGenerateRequest>& requests) {
  std::size_t count = 0U;
  for (const auto& request : requests) {
    if (request.stage == "reflection") {
      ++count;
    }
  }

  return count;
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
      .source_ref = {.ref_type = "observation", .ref_id = "obs-reflection-budget-cap"},
  };
  return observation;
}

[[nodiscard]] dasall::cognition::plan::PlanGraph make_active_plan() {
  dasall::cognition::plan::PlanGraph active_plan;
  active_plan.plan_id = "plan-reflection-budget-cap";
  active_plan.revision = 2U;
  active_plan.nodes = {
      dasall::cognition::plan::PlanNode{
          .node_id = "plan-node:reflection-budget-cap",
          .objective = "repair the reasoning path before runtime considers recovery",
          .success_signal = "reflection emits a stable replan suggestion",
          .action_kind_hint = "reasoning",
          .depends_on = {},
          .evidence_refs = {"tests:reflection-budget-cap"},
      },
  };
  active_plan.plan_rationale = "tight-budget profile should cap self-refine rounds";
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
      + "\"confidence\":0.86,"
      + "\"relevant_observation_refs\":[\"obs-reflection-budget-cap\"],"
      + "\"hint_ref\":\"" + hint_ref + "\","
      + "\"created_at\":1712746800000,"
      + "\"tags\":[\"cognition\",\"reflection\"]}"
      ;
}

void test_reflection_self_refine_skips_second_round_when_budget_is_capped() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-reflection-self-refine-budget-cap",
      .trace_id = "trace-reflection-self-refine-budget-cap",
      .profile_id = "edge_minimal",
      .goal_id = "goal-reflection-self-refine-budget-cap",
      .observation_id = "obs-reflection-budget-cap",
  });

  auto first_payload = make_reflection_payload(
      fixture.options(),
      "Continue",
      "tight budget should retain the first reflection decision without a second round",
      "hint:reflection:continue");
  auto second_payload = make_reflection_payload(
      fixture.options(),
      "Replan",
      "this payload should never be consumed because the second round is capped",
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
              "budget-capped reflection should stay on the primary success path");
  assert_true(result.reflection_decision.has_value(),
              "budget-capped reflection should still produce a reflection decision");
  assert_true(result.reflection_decision->decision_kind.has_value() &&
                  *result.reflection_decision->decision_kind == ReflectionDecisionKind::Continue,
              "budget cap should preserve the first reflection decision as final output");
  assert_true(contains_value(result.diagnostics,
                             "reflection_pipeline.self_refine.skipped:budget_cap"),
              "budget-capped reflection should expose an explicit skip diagnostic");
  assert_true(!contains_value(result.diagnostics,
                              "reflection_pipeline.self_refine.completed"),
              "budget-capped reflection must not mark self-refine as completed");
  assert_true(count_reflection_requests(fixture.llm_manager()->generate_requests()) == 1U,
              "budget-capped reflection must not dispatch a second bridge request");
}

}  // namespace

int main() {
  try {
    test_reflection_self_refine_skips_second_round_when_budget_is_capped();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}