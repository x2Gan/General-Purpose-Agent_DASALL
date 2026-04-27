#include <exception>
#include <iostream>
#include <string>

#include "checkpoint/ReflectionDecisionGuards.h"
#include "reflection/ReflectionEngine.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::ReflectionAnalysisRequest;
using dasall::cognition::reflection::ReflectionEngine;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::validate_reflection_decision_field_rules;
using dasall::tests::support::assert_true;

[[nodiscard]] ReflectionAnalysisRequest make_base_request() {
  ReflectionAnalysisRequest request;
  request.caller_domain = "runtime.recovery_manager";
  request.request_id = "req-017-retry";
  request.trace_id = "trace-017-retry";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-017-retry");
  request.goal_contract.request_id = std::string("req-017-retry");
  request.goal_contract.goal_description =
      std::string("collect verifiable quarterly sales evidence for Berlin");
  request.goal_contract.success_criteria =
      std::string("return verified Berlin quarterly sales evidence");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.belief_state.request_id = std::string("req-017-retry");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the target city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("dataset lookup should produce a sales table")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("dataset access is available")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:retry")};
  request.belief_state.confidence = 0.81F;
  request.belief_state.goal_id = std::string("goal-017-retry");
  request.belief_state.created_at = 1712345600100;

  request.latest_observation.observation_id = std::string("obs-017-retry");
  request.latest_observation.success = false;
  request.latest_observation.payload =
      std::string("dataset request timed out while collecting berlin quarterly sales");
  request.latest_observation.created_at = 1712345602000;

  request.error_info = dasall::contracts::ErrorInfo{
      .failure_type = ResultCodeCategory::Tool,
      .retryable = true,
      .safe_to_replan = true,
      .details = {.code = 408,
                  .message = "dataset request timed out while collecting berlin quarterly sales",
                  .stage = "tool_execution"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-017-retry"},
  };

  request.active_plan = dasall::cognition::plan::PlanGraph{};
  request.active_plan->plan_id = std::string("plan-017-retry");
  request.active_plan->revision = 1U;
  request.active_plan->nodes = {
      {.node_id = "plan-017-retry-node",
       .objective = "query dataset sales table for Berlin",
       .success_signal = "verified Berlin sales evidence is returned",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:retry")}},
  };
  request.active_plan->plan_rationale = std::string("planner built a single dataset lookup step");
  request.active_plan->estimated_complexity = 1U;
  return request;
}

void test_reflection_engine_prefers_retry_step_for_retryable_local_failures() {
  ReflectionEngine engine(CognitionConfig{});
  const auto decision = engine.analyze(make_base_request());
  const auto guard = validate_reflection_decision_field_rules(decision);

  assert_true(guard.ok,
              "reflection output should stay within the frozen ReflectionDecision contract");
  assert_true(decision.decision_kind == ReflectionDecisionKind::RetryStep,
              "retryable local failures should map to RetryStep");
  assert_true(decision.hint_ref.has_value() &&
                  decision.hint_ref->find("retry_step") != std::string::npos,
              "retry decisions should carry a retry-step hint ref");
  assert_true(decision.relevant_observation_refs.has_value() &&
                  decision.relevant_observation_refs->front() == "obs-017-retry",
              "retry decisions should preserve the failing observation id");
  assert_true(decision.rationale.has_value() &&
                  decision.rationale->find("retryable") != std::string::npos,
              "retry decisions should explain the retryable failure rationale");
}

void test_reflection_engine_continues_when_observation_is_successful() {
  ReflectionEngine engine(CognitionConfig{});
  auto request = make_base_request();
  request.latest_observation.success = true;
  request.latest_observation.payload =
      std::string("verified Berlin quarterly sales evidence was returned successfully");
  request.error_info.reset();

  const auto decision = engine.analyze(request);

  assert_true(decision.decision_kind == ReflectionDecisionKind::Continue,
              "successful observations should keep the reflection decision in Continue");
}

}  // namespace

int main() {
  try {
    test_reflection_engine_prefers_retry_step_for_retryable_local_failures();
    test_reflection_engine_continues_when_observation_is_successful();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}