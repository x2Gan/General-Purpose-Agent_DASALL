#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "support/TestAssertions.h"
#include "task/WorkerTask.h"
#include "task/WorkerTaskGuards.h"

namespace {

using dasall::contracts::WorkerTask;
using dasall::contracts::validate_worker_task_boundary;
using dasall::contracts::validate_worker_task_forbidden_field;
using dasall::contracts::validate_worker_task_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

template <typename T, typename = void>
struct has_session_id : std::false_type {};

template <typename T>
struct has_session_id<T, std::void_t<decltype(std::declval<T>().session_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_checkpoint_ref : std::false_type {};

template <typename T>
struct has_checkpoint_ref<
    T,
    std::void_t<decltype(std::declval<T>().checkpoint_ref)>> : std::true_type {
};

template <typename T, typename = void>
struct has_global_fsm_state : std::false_type {};

template <typename T>
struct has_global_fsm_state<
    T,
    std::void_t<decltype(std::declval<T>().global_fsm_state)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_agent_result : std::false_type {};

template <typename T>
struct has_agent_result<T, std::void_t<decltype(std::declval<T>().agent_result)>>
    : std::true_type {};

WorkerTask make_valid_worker_task() {
  WorkerTask task;
  task.task_id = "worker-task-001";
  task.parent_task_id = "parent-task-001";
  task.lease_id = "lease-001";
  task.worker_type = "analysis_worker";
  task.allowed_tools = std::vector<std::string>{"grep_search", "read_file"};
  task.timeout_ms = 15000U;
  task.idempotency_key = "worker-task-idem-001";
  return task;
}

void test_valid_worker_task_passes_required_fields_guard() {
  const auto task = make_valid_worker_task();
  const auto result = validate_worker_task_required_fields(task);
  assert_true(result.ok, "valid worker task should pass required fields guard");
}

void test_valid_worker_task_passes_boundary_guard() {
  const auto task = make_valid_worker_task();
  const auto result = validate_worker_task_boundary(task);
  assert_true(result.ok, "valid worker task should pass boundary guard");
}

void test_missing_task_id_fails_required_fields_guard() {
  auto task = make_valid_worker_task();
  task.task_id = std::nullopt;

  const auto result = validate_worker_task_required_fields(task);
  assert_true(!result.ok, "missing task_id must fail required guard");
  assert_equal("task_id is required and must be non-empty",
               std::string(result.reason),
               "missing task_id must return canonical reason");
}

void test_empty_allowed_tools_fails_required_fields_guard() {
  auto task = make_valid_worker_task();
  task.allowed_tools = std::vector<std::string>{};

  const auto result = validate_worker_task_required_fields(task);
  assert_true(!result.ok, "empty allowed_tools must fail required guard");
  assert_equal("allowed_tools are required and must contain at least one item",
               std::string(result.reason),
               "empty allowed_tools must return canonical reason");
}

void test_parent_and_task_anchor_collapse_fails_boundary_guard() {
  auto task = make_valid_worker_task();
  task.parent_task_id = task.task_id;

  const auto result = validate_worker_task_boundary(task);
  assert_true(!result.ok, "task/parent anchor collapse must fail boundary guard");
  assert_equal(
      "task_id must not equal parent_task_id because worker and parent anchors are layered separately",
      std::string(result.reason),
      "anchor collapse must return canonical reason");
}

void test_global_fsm_state_alias_is_rejected_by_shared_boundary_guard() {
  const auto result = validate_worker_task_forbidden_field("global_fsm_state");
  assert_true(!result.ok, "global_fsm_state must be rejected for WorkerTask");
  assert_equal("worker task must not carry global session or fsm state",
               std::string(result.reason),
               "shared boundary guard should keep normalized ADR-008 rejection reason");
}

void test_compile_time_shape_does_not_reuse_global_or_result_fields() {
  static_assert(!has_session_id<WorkerTask>::value,
                "WorkerTask must not expose session_id");
  static_assert(!has_checkpoint_ref<WorkerTask>::value,
                "WorkerTask must not expose checkpoint_ref");
  static_assert(!has_global_fsm_state<WorkerTask>::value,
                "WorkerTask must not expose global_fsm_state");
  static_assert(!has_agent_result<WorkerTask>::value,
                "WorkerTask must not expose agent_result");
}

}  // namespace

int main() {
  try {
    test_valid_worker_task_passes_required_fields_guard();
    test_valid_worker_task_passes_boundary_guard();
    test_missing_task_id_fails_required_fields_guard();
    test_empty_allowed_tools_fails_required_fields_guard();
    test_parent_and_task_anchor_collapse_fails_boundary_guard();
    test_global_fsm_state_alias_is_rejected_by_shared_boundary_guard();
    test_compile_time_shape_does_not_reuse_global_or_result_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}