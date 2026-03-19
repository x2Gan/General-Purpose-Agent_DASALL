// ============================================================================
// WorkerTaskFieldContractTest.cpp
//
// WP04-T019-B: Field-level contract test for WorkerTaskGuards.h.
//
// Validates the T019 field-table rules layered on top of the T018 object guard:
//   - required string slots must contain non-whitespace content.
//   - allowed_tools must not contain whitespace-only or duplicate items.
//   - task_id and parent_task_id must stay distinct after trimming whitespace.
//   - idempotency_key, if present, must contain non-whitespace content.
// ============================================================================

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "dasall/tests/support/TestAssertions.h"
#include "task/WorkerTask.h"
#include "task/WorkerTaskGuards.h"

namespace {

using dasall::contracts::WorkerTask;
using dasall::contracts::validate_worker_task_field_rules;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

WorkerTask make_valid_worker_task() {
  WorkerTask task;
  task.task_id = "worker-task-019";
  task.parent_task_id = "parent-task-019";
  task.lease_id = "lease-019";
  task.worker_type = "analysis_worker";
  task.allowed_tools = std::vector<std::string>{"grep_search", "read_file"};
  task.timeout_ms = 12000U;
  task.idempotency_key = "idem-019";
  return task;
}

void test_valid_worker_task_passes_field_rules() {
  const auto task = make_valid_worker_task();
  const auto result = validate_worker_task_field_rules(task);

  assert_true(result.ok, "valid WorkerTask should pass field rules");
}

void test_whitespace_only_task_id_is_rejected() {
  auto task = make_valid_worker_task();
  task.task_id = "   \t";

  const auto result = validate_worker_task_field_rules(task);
  assert_true(!result.ok, "whitespace-only task_id must be rejected");
  assert_equal("task_id must contain at least one non-whitespace character",
               std::string(result.reason),
               "task_id whitespace failure must return canonical reason");
}

void test_whitespace_only_worker_type_is_rejected() {
  auto task = make_valid_worker_task();
  task.worker_type = "\n  ";

  const auto result = validate_worker_task_field_rules(task);
  assert_true(!result.ok, "whitespace-only worker_type must be rejected");
  assert_equal(
      "worker_type must contain at least one non-whitespace character",
      std::string(result.reason),
      "worker_type whitespace failure must return canonical reason");
}

void test_whitespace_only_allowed_tool_is_rejected() {
  auto task = make_valid_worker_task();
  task.allowed_tools = std::vector<std::string>{"grep_search", "   "};

  const auto result = validate_worker_task_field_rules(task);
  assert_true(!result.ok,
              "whitespace-only allowed_tools item must be rejected");
  assert_equal(
      "allowed_tools must not contain empty or whitespace-only items",
      std::string(result.reason),
      "allowed_tools whitespace failure must return canonical reason");
}

void test_duplicate_allowed_tools_are_rejected() {
  auto task = make_valid_worker_task();
  task.allowed_tools = std::vector<std::string>{"read_file", " read_file "};

  const auto result = validate_worker_task_field_rules(task);
  assert_true(!result.ok, "duplicate allowed_tools must be rejected");
  assert_equal("allowed_tools must not contain duplicate items",
               std::string(result.reason),
               "duplicate allowed_tools must return canonical reason");
}

void test_trimmed_task_and_parent_anchor_collapse_is_rejected() {
  auto task = make_valid_worker_task();
  task.task_id = " shared-anchor ";
  task.parent_task_id = "shared-anchor";

  const auto result = validate_worker_task_field_rules(task);
  assert_true(!result.ok,
              "trimmed task/parent anchor collapse must be rejected");
  assert_equal(
      "task_id and parent_task_id must remain distinct after trimming whitespace",
      std::string(result.reason),
      "trimmed anchor collapse must return canonical reason");
}

void test_whitespace_only_idempotency_key_is_rejected() {
  auto task = make_valid_worker_task();
  task.idempotency_key = "  \n";

  const auto result = validate_worker_task_field_rules(task);
  assert_true(!result.ok,
              "whitespace-only idempotency_key must be rejected");
  assert_equal(
      "idempotency_key must contain at least one non-whitespace character when present",
      std::string(result.reason),
      "idempotency_key whitespace failure must return canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_worker_task_passes_field_rules();
    test_whitespace_only_task_id_is_rejected();
    test_whitespace_only_worker_type_is_rejected();
    test_whitespace_only_allowed_tool_is_rejected();
    test_duplicate_allowed_tools_are_rejected();
    test_trimmed_task_and_parent_anchor_collapse_is_rejected();
    test_whitespace_only_idempotency_key_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}