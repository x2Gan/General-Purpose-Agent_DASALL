// ============================================================================
// MultiAgentResultFieldContractTest.cpp
//
// WP04-T017-B: Field-level contract test for MultiAgentResultGuards.h.
//
// Validates the T017 field-table rules layered on top of the T016 object guard:
//   - aggregation strings must contain non-whitespace content.
//   - aggregation vectors must not contain whitespace-only or duplicate items.
//   - merged_result and recommended_next_action must stay semantically distinct.
// ============================================================================

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/MultiAgentResult.h"
#include "agent/MultiAgentResultGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::MultiAgentResult;
using dasall::contracts::validate_multi_agent_result_field_rules;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

MultiAgentResult make_valid_result() {
  MultiAgentResult result;
  result.subtask_results = std::vector<std::string>{
      "worker-a: normalized summary",
      "worker-b: normalized summary",
  };
  result.merged_result = "merged collaboration recommendation";
  result.recommended_next_action = "fold_into_agent_result";
  result.conflicts = std::vector<std::string>{"ranking mismatch"};
  result.worker_trace_refs =
      std::vector<std::string>{"trace://worker-a", "trace://worker-b"};
  result.failure_summary = "worker-c timed out during evidence collection";
  return result;
}

void test_valid_result_passes_field_rules() {
  const auto result = make_valid_result();
  const auto guard_result = validate_multi_agent_result_field_rules(result);

  assert_true(guard_result.ok,
              "valid MultiAgentResult should pass field rules");
}

void test_whitespace_only_merged_result_is_rejected() {
  auto result = make_valid_result();
  result.merged_result = "  \t  ";

  const auto guard_result = validate_multi_agent_result_field_rules(result);
  assert_true(!guard_result.ok,
              "whitespace-only merged_result must be rejected");
  assert_equal(
      "merged_result must contain at least one non-whitespace character",
      std::string(guard_result.reason),
      "merged_result whitespace failure must return canonical reason");
}

void test_duplicate_subtask_results_are_rejected() {
  auto result = make_valid_result();
  result.subtask_results = std::vector<std::string>{
      "worker-a: normalized summary",
      " worker-a: normalized summary ",
  };

  const auto guard_result = validate_multi_agent_result_field_rules(result);
  assert_true(!guard_result.ok,
              "duplicate subtask_results must be rejected");
  assert_equal("subtask_results must not contain duplicate items",
               std::string(guard_result.reason),
               "duplicate subtask_results must return canonical reason");
}

void test_whitespace_only_conflict_item_is_rejected() {
  auto result = make_valid_result();
  result.conflicts = std::vector<std::string>{"   "};

  const auto guard_result = validate_multi_agent_result_field_rules(result);
  assert_true(!guard_result.ok,
              "whitespace-only conflict item must be rejected");
  assert_equal(
      "conflicts must not contain empty or whitespace-only items",
      std::string(guard_result.reason),
      "conflicts whitespace failure must return canonical reason");
}

void test_duplicate_worker_trace_refs_are_rejected() {
  auto result = make_valid_result();
  result.worker_trace_refs =
      std::vector<std::string>{"trace://worker-a", "trace://worker-a"};

  const auto guard_result = validate_multi_agent_result_field_rules(result);
  assert_true(!guard_result.ok,
              "duplicate worker_trace_refs must be rejected");
  assert_equal("worker_trace_refs must not contain duplicate items",
               std::string(guard_result.reason),
               "duplicate worker_trace_refs must return canonical reason");
}

void test_merged_result_and_recommended_next_action_collapse_is_rejected() {
  auto result = make_valid_result();
  result.merged_result = " continue_with_merge ";
  result.recommended_next_action = "continue_with_merge";

  const auto guard_result = validate_multi_agent_result_field_rules(result);
  assert_true(!guard_result.ok,
              "merged_result/recommended_next_action collapse must be rejected");
  assert_equal(
      "merged_result and recommended_next_action must remain distinct after trimming whitespace",
      std::string(guard_result.reason),
      "result/action collapse must return canonical reason");
}

void test_whitespace_only_failure_summary_is_rejected() {
  auto result = make_valid_result();
  result.failure_summary = "\n\t  ";

  const auto guard_result = validate_multi_agent_result_field_rules(result);
  assert_true(!guard_result.ok,
              "whitespace-only failure_summary must be rejected");
  assert_equal(
      "failure_summary must contain at least one non-whitespace character when present",
      std::string(guard_result.reason),
      "failure_summary whitespace failure must return canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_result_passes_field_rules();
    test_whitespace_only_merged_result_is_rejected();
    test_duplicate_subtask_results_are_rejected();
    test_whitespace_only_conflict_item_is_rejected();
    test_duplicate_worker_trace_refs_are_rejected();
    test_merged_result_and_recommended_next_action_collapse_is_rejected();
    test_whitespace_only_failure_summary_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}