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
  request.request_id = "req-017-abort";
  request.trace_id = "trace-017-abort";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-017-abort");
  request.goal_contract.request_id = std::string("req-017-abort");
  request.goal_contract.goal_description =
      std::string("collect verifiable quarterly sales evidence for Berlin");
  request.goal_contract.success_criteria =
      std::string("return verified Berlin quarterly sales evidence");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.belief_state.request_id = std::string("req-017-abort");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("external billing side effects already started")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("a second retry could duplicate external writes")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("retry is still safe")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:abort")};
  request.belief_state.confidence = 0.62F;
  request.belief_state.goal_id = std::string("goal-017-abort");
  request.belief_state.created_at = 1712345600100;

  request.latest_observation.observation_id = std::string("obs-017-abort");
  request.latest_observation.success = false;
  request.latest_observation.payload = std::string(
      "manual intervention required because irreversible side effect was already committed");
  request.latest_observation.side_effects =
      std::vector<std::string>{std::string("external_write:invoice-42")};
  request.latest_observation.created_at = 1712345602000;

  request.error_info = dasall::contracts::ErrorInfo{
      .failure_type = ResultCodeCategory::Policy,
      .retryable = false,
      .safe_to_replan = false,
      .details = {.code = 423,
                  .message = "manual intervention required because irreversible side effect was already committed",
                  .stage = "runtime_recovery"},
      .source_ref = {.ref_type = "observation", .ref_id = "obs-017-abort"},
  };
  request.execution_hints.risk_tolerance = 0.0F;

  request.active_plan = dasall::cognition::plan::PlanGraph{};
  request.active_plan->plan_id = std::string("plan-017-abort");
  request.active_plan->revision = 2U;
  request.active_plan->nodes = {
      {.node_id = "plan-017-abort-node",
       .objective = "submit billing update for Berlin customer",
       .success_signal = "billing update completes exactly once",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:abort")}},
  };
  request.active_plan->plan_rationale = std::string("planner built a billing update step");
  request.active_plan->estimated_complexity = 1U;
  return request;
}

void test_reflection_engine_aborts_safe_when_risk_is_high() {
  ReflectionEngine engine(CognitionConfig{});
  const auto decision = engine.analyze(make_request());
  const auto guard = validate_reflection_decision_field_rules(decision);

  assert_true(guard.ok,
              "abort-safe decision should remain a valid ReflectionDecision object");
  assert_true(decision.decision_kind == ReflectionDecisionKind::AbortSafe,
              "high-risk recovery scenarios should collapse to AbortSafe");
  assert_true(decision.confidence.has_value() && *decision.confidence <= 0.35F,
              "abort-safe should stay conservative under high risk");
  assert_true(decision.rationale.has_value() &&
                  decision.rationale->find("risk") != std::string::npos,
              "abort-safe rationale should explain the risk-based downgrade");
  assert_true(decision.hint_ref.has_value() &&
                  decision.hint_ref->find("abort_safe") != std::string::npos,
              "abort-safe decisions should expose the abort-safe hint ref");
}

}  // namespace

int main() {
  try {
    test_reflection_engine_aborts_safe_when_risk_is_high();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}