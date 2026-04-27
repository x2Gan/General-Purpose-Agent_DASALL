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

[[nodiscard]] ReflectionAnalysisRequest make_request() {
  ReflectionAnalysisRequest request;
  request.caller_domain = "runtime.recovery_manager";
  request.request_id = "req-017-replan";
  request.trace_id = "trace-017-replan";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-017-replan");
  request.goal_contract.request_id = std::string("req-017-replan");
  request.goal_contract.goal_description =
      std::string("collect verifiable quarterly sales evidence for Berlin");
  request.goal_contract.success_criteria =
      std::string("return verified Berlin quarterly sales evidence");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.belief_state.request_id = std::string("req-017-replan");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the target city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("dataset lookup should produce a sales table")};
  request.belief_state.assumptions = std::vector<std::string>{
      std::string("dataset access is available"),
      std::string("source schema matches the current extraction template"),
  };
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:replan")};
  request.belief_state.confidence = 0.74F;
  request.belief_state.goal_id = std::string("goal-017-replan");
  request.belief_state.created_at = 1712345600100;

  request.latest_observation.observation_id = std::string("obs-017-replan");
  request.latest_observation.success = false;
  request.latest_observation.payload = std::string(
      "dataset access is not available and the source schema changed for this request");
  request.latest_observation.created_at = 1712345602000;

  request.error_info = dasall::contracts::ErrorInfo{
      .failure_type = ResultCodeCategory::Tool,
      .retryable = false,
      .safe_to_replan = true,
      .details = {.code = 503,
                  .message = "dataset access is not available and the source schema changed",
                  .stage = "tool_execution"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-017-replan"},
  };

  request.active_plan = dasall::cognition::plan::PlanGraph{};
  request.active_plan->plan_id = std::string("plan-017-replan");
  request.active_plan->revision = 1U;
  request.active_plan->nodes = {
      {.node_id = "plan-017-replan-node",
       .objective = "query dataset sales table for Berlin",
       .success_signal = "verified Berlin sales evidence is returned",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:replan")}},
  };
  request.active_plan->plan_rationale = std::string("planner built a single dataset lookup step");
  request.active_plan->estimated_complexity = 1U;
  return request;
}

void test_reflection_engine_replans_when_assumptions_are_invalidated() {
  ReflectionEngine engine(CognitionConfig{});
  const auto decision = engine.analyze(make_request());
  const auto guard = validate_reflection_decision_field_rules(decision);

  assert_true(guard.ok,
              "replan decision should remain a valid ReflectionDecision object");
  assert_true(decision.decision_kind == ReflectionDecisionKind::Replan,
              "invalidated assumptions should push reflection toward Replan");
  assert_true(decision.hint_ref.has_value() &&
                  decision.hint_ref->find("replan") != std::string::npos,
              "replan decisions should carry a replan hint ref");
  assert_true(decision.confidence.has_value() && *decision.confidence >= 0.65F,
              "belief invalidation should produce a confident replan suggestion");
  assert_true(decision.rationale.has_value() &&
                  decision.rationale->find("invalidated assumptions") != std::string::npos,
              "replan rationale should describe the invalidated assumptions");
}

}  // namespace

int main() {
  try {
    test_reflection_engine_replans_when_assumptions_are_invalidated();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}