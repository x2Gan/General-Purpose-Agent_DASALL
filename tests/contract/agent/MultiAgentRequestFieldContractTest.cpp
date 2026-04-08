// ============================================================================
// MultiAgentRequestFieldContractTest.cpp
//
// WP04-T015-B: Field-level contract test for MultiAgentRequestGuards.h.
//
// Validates the T015 field-table rules layered on top of the T014 object guard:
//   - required string slots must contain non-whitespace content.
//   - worker_budget_guard, if present, must satisfy RuntimeBudget guards.
//   - permission_guard, if present, must contain non-whitespace content.
//   - stop_conditions, if present, must be non-empty, non-whitespace, unique.
//   - goal_fragment and plan_fragment must stay semantically distinct.
//   - handoff mode must carry explicit permission_guard and stop_conditions.
// ============================================================================

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/MultiAgentRequest.h"
#include "agent/MultiAgentRequestGuards.h"
#include "checkpoint/RuntimeBudget.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CollaborationMode;
using dasall::contracts::MultiAgentRequest;
using dasall::contracts::RuntimeBudget;
using dasall::contracts::validate_multi_agent_request_field_rules;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

RuntimeBudget make_valid_worker_budget_guard() {
  RuntimeBudget budget;
  budget.max_tokens = 4096;
  budget.max_turns = 6;
  budget.max_tool_calls = 8;
  budget.max_latency_ms = 20000;
  budget.max_replan_count = 2;
  return budget;
}

MultiAgentRequest make_valid_request() {
  MultiAgentRequest request;
  request.parent_request_id = "req-015";
  request.parent_task_id = "task-015-root";
  request.goal_fragment = "compare specialist outputs for the active subgoal";
  request.plan_fragment =
      "fan out to analysis workers and aggregate their normalized findings";
  request.collaboration_mode = CollaborationMode::Concurrent;
  request.worker_budget_guard = make_valid_worker_budget_guard();
  request.permission_guard = "analysis.readonly";
  request.stop_conditions =
      std::vector<std::string>{"all-workers-complete", "conflict-threshold-reached"};
  return request;
}

void test_valid_request_passes_field_rules() {
  const auto request = make_valid_request();
  const auto result = validate_multi_agent_request_field_rules(request);

  assert_true(result.ok,
              "valid MultiAgentRequest should pass field rules");
}

void test_whitespace_only_goal_fragment_is_rejected() {
  auto request = make_valid_request();
  request.goal_fragment = "   \t";

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "whitespace-only goal_fragment must be rejected");
  assert_equal(
      "goal_fragment must contain at least one non-whitespace character",
      std::string(result.reason),
      "goal_fragment whitespace failure must return canonical reason");
}

void test_invalid_worker_budget_guard_is_rejected() {
  auto request = make_valid_request();
  request.worker_budget_guard->max_tool_calls = 0U;

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "invalid worker_budget_guard must be rejected");
  assert_equal(
      "worker_budget_guard must pass nested RuntimeBudget validation",
      std::string(result.reason),
      "invalid worker budget guard must return canonical reason");
}

void test_whitespace_only_permission_guard_is_rejected() {
  auto request = make_valid_request();
  request.permission_guard = "   ";

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "whitespace-only permission_guard must be rejected");
  assert_equal(
      "permission_guard must contain at least one non-whitespace character when present",
      std::string(result.reason),
      "permission_guard whitespace failure must return canonical reason");
}

void test_duplicate_stop_conditions_are_rejected() {
  auto request = make_valid_request();
  request.stop_conditions =
      std::vector<std::string>{"all-workers-complete", "all-workers-complete"};

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "duplicate stop_conditions must be rejected");
  assert_equal("stop_conditions must not contain duplicate items",
               std::string(result.reason),
               "duplicate stop_conditions must return canonical reason");
}

void test_goal_and_plan_fragment_collapse_is_rejected() {
  auto request = make_valid_request();
  request.goal_fragment = " shared-scope ";
  request.plan_fragment = "shared-scope";

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "goal_fragment and plan_fragment collapse must be rejected");
  assert_equal(
      "goal_fragment and plan_fragment must remain distinct after trimming whitespace",
      std::string(result.reason),
      "goal/plan collapse must return canonical reason");
}

void test_handoff_requires_permission_guard() {
  auto request = make_valid_request();
  request.collaboration_mode = CollaborationMode::Handoff;
  request.permission_guard = std::nullopt;

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "handoff mode must require permission_guard");
  assert_equal(
      "permission_guard is required when collaboration_mode is Handoff",
      std::string(result.reason),
      "handoff permission_guard failure must return canonical reason");
}

void test_handoff_requires_stop_conditions() {
  auto request = make_valid_request();
  request.collaboration_mode = CollaborationMode::Handoff;
  request.stop_conditions = std::nullopt;

  const auto result = validate_multi_agent_request_field_rules(request);
  assert_true(!result.ok,
              "handoff mode must require stop_conditions");
  assert_equal(
      "stop_conditions are required when collaboration_mode is Handoff",
      std::string(result.reason),
      "handoff stop_conditions failure must return canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_request_passes_field_rules();
    test_whitespace_only_goal_fragment_is_rejected();
    test_invalid_worker_budget_guard_is_rejected();
    test_whitespace_only_permission_guard_is_rejected();
    test_duplicate_stop_conditions_are_rejected();
    test_goal_and_plan_fragment_collapse_is_rejected();
    test_handoff_requires_permission_guard();
    test_handoff_requires_stop_conditions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}