#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ICognitionEngine.h"
#include "error/ResultCode.h"
#include "support/TestAssertions.h"
#include "MockCognitionFixture.h"

namespace {

using dasall::contracts::ReflectionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
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

[[nodiscard]] dasall::cognition::plan::PlanGraph make_active_plan() {
  dasall::cognition::plan::PlanGraph active_plan;
  active_plan.plan_id = "plan-reflection-structured";
  active_plan.revision = 1U;
  active_plan.nodes = {
      dasall::cognition::plan::PlanNode{
          .node_id = "plan-node:reflection-structured",
          .objective = "retry the failed tool path once evidence is refreshed",
          .success_signal = "tool path recovered",
          .action_kind_hint = "tool_execution",
          .depends_on = {},
          .evidence_refs = {"tests:reflection-structured-output"},
      },
  };
  active_plan.plan_rationale = "reflection integration should preserve active plan context";
  active_plan.estimated_complexity = 1U;
  return active_plan;
}

[[nodiscard]] dasall::contracts::Observation make_failed_observation(
    const MockCognitionFixture& fixture) {
  auto observation = fixture.make_observation(
      false, "dataset request timed out while collecting governed evidence");
  observation.error = dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Tool,
      .retryable = true,
      .safe_to_replan = true,
      .details = {.code = 408,
                  .message = "dataset request timed out while collecting governed evidence",
                  .stage = "tool_execution"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-reflection-structured"},
  };
  return observation;
}

[[nodiscard]] std::string make_valid_reflection_payload(
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
      + "\"confidence\":0.91," 
      + "\"relevant_observation_refs\":[\"obs-reflection-structured\"]," 
      + "\"hint_ref\":\"" + hint_ref + "\"," 
      + "\"created_at\":1712746800000," 
      + "\"tags\":[\"cognition\",\"reflection\"]}"
      ;
}

void test_reflection_projects_bridge_payload_authoritatively() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-reflection-structured-authoritative",
      .trace_id = "trace-reflection-structured-authoritative",
      .goal_id = "goal-reflection-structured-authoritative",
  });
  fixture.llm_manager()->set_structured_stage_payload(
      "reflection",
      make_valid_reflection_payload(
          fixture.options(),
          "AbortSafe",
          "bridge-authored reflection decision should override the local retry heuristic",
          "hint:reflection:abort_safe"),
      fixture.options().request_id);

  auto engine = fixture.make_engine();
  auto request = fixture.make_reflection_request(make_failed_observation(fixture));
  request.active_plan = make_active_plan();
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->reflect(request);

  assert_true(!result.result_code.has_value(),
              "valid structured reflection payload should stay on the success path");
  assert_true(result.reflection_decision.has_value(),
              "valid structured reflection payload should yield a reflection decision");
  assert_true(result.reflection_decision->decision_kind.has_value() &&
                  *result.reflection_decision->decision_kind == ReflectionDecisionKind::AbortSafe,
              "bridge reflection payload should become the authoritative decision source");
  assert_true(result.reflection_decision->rationale.has_value() &&
                  result.reflection_decision->rationale->find("override the local retry heuristic") !=
                      std::string::npos,
              "authoritative reflection payload should preserve the bridge-authored rationale");
  assert_true(result.reflection_decision->hint_ref.has_value() &&
                  *result.reflection_decision->hint_ref == "hint:reflection:abort_safe",
              "authoritative reflection payload should preserve the bridge-authored hint_ref");
  assert_true(result.belief_update_hint.has_value(),
              "authoritative reflection payload should still synthesize a belief update hint");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.projected_reflection_decision"),
              "reflection integration should mark the authoritative structured projection");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.local_fallback:reflection"),
              "authoritative reflection payload must not degrade to the local engine");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "reflection"),
              "reflection integration should dispatch the canonical reflection stage");
}

void test_reflection_degrades_to_local_engine_when_bridge_payload_is_schema_invalid() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-reflection-structured-fallback",
      .trace_id = "trace-reflection-structured-fallback",
      .goal_id = "goal-reflection-structured-fallback",
  });
  fixture.llm_manager()->set_structured_stage_payload(
      "reflection",
      std::string{"{"}
          + "\"schema_version\":\"cognition.reflection.v2\"," 
          + "\"request_id\":\"" + fixture.options().request_id + "\"," 
          + "\"decision_kind\":\"AbortSafe\"," 
          + "\"rationale\":\"schema drift should trigger explicit fallback\"}",
      fixture.options().request_id);

  auto engine = fixture.make_engine();
  auto request = fixture.make_reflection_request(make_failed_observation(fixture));
  request.active_plan = make_active_plan();

  const auto result = engine->reflect(request);

  assert_true(!result.result_code.has_value(),
              "schema-invalid reflection payload should degrade to the local engine when allowed");
  assert_true(result.reflection_decision.has_value(),
              "schema-invalid reflection payload should still yield a local reflection decision");
  assert_true(result.reflection_decision->decision_kind.has_value() &&
                  *result.reflection_decision->decision_kind == ReflectionDecisionKind::RetryStep,
              "schema-invalid reflection payload should fall back to the local retry_step heuristic");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.schema_violation:reflection"),
              "schema-invalid reflection payload should surface the schema violation diagnostic");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.local_fallback:reflection"),
              "schema-invalid reflection payload should record the explicit local fallback");
  assert_true(!contains_value(result.diagnostics,
                              "structured_projection.projected_reflection_decision"),
              "schema-invalid reflection payload must not be marked authoritative");
}

}  // namespace

int main() {
  try {
    test_reflection_projects_bridge_payload_authoritatively();
    test_reflection_degrades_to_local_engine_when_bridge_payload_is_schema_invalid();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}