#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "agent/MultiAgentResult.h"
#include "agent/MultiAgentResultGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::MultiAgentResult;
using dasall::contracts::validate_multi_agent_result_boundary;
using dasall::contracts::validate_multi_agent_result_forbidden_field;
using dasall::contracts::validate_multi_agent_result_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

template <typename T, typename = void>
struct has_result_code : std::false_type {};

template <typename T>
struct has_result_code<T, std::void_t<decltype(std::declval<T>().result_code)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_response_text : std::false_type {};

template <typename T>
struct has_response_text<
    T,
    std::void_t<decltype(std::declval<T>().response_text)>> : std::true_type {};

template <typename T, typename = void>
struct has_task_completed : std::false_type {};

template <typename T>
struct has_task_completed<
    T,
    std::void_t<decltype(std::declval<T>().task_completed)>> : std::true_type {};

template <typename T, typename = void>
struct has_error_info : std::false_type {};

template <typename T>
struct has_error_info<T, std::void_t<decltype(std::declval<T>().error_info)>>
    : std::true_type {};

MultiAgentResult make_valid_multi_agent_result() {
  MultiAgentResult result;
  result.subtask_results =
      std::vector<std::string>{"worker-a: summary", "worker-b: summary"};
  result.merged_result = "merged collaboration output";
  result.recommended_next_action = "fold_into_agent_result";
  return result;
}

void test_valid_multi_agent_result_passes_required_fields_guard() {
  const auto result = make_valid_multi_agent_result();
  const auto guard_result = validate_multi_agent_result_required_fields(result);
  assert_true(guard_result.ok,
              "valid multi-agent result should pass required fields guard");
}

void test_valid_multi_agent_result_passes_boundary_guard() {
  auto result = make_valid_multi_agent_result();
  result.conflicts = std::vector<std::string>{"ranking mismatch"};
  result.worker_trace_refs =
      std::vector<std::string>{"trace://worker-a", "trace://worker-b"};
  result.failure_summary = "worker-c timed out and was excluded from merge";

  const auto guard_result = validate_multi_agent_result_boundary(result);
  assert_true(guard_result.ok,
              "valid multi-agent result should pass boundary guard");
}

void test_missing_subtask_results_fails() {
  auto result = make_valid_multi_agent_result();
  result.subtask_results = std::nullopt;

  const auto guard_result = validate_multi_agent_result_required_fields(result);
  assert_true(!guard_result.ok,
              "missing subtask_results should fail required fields guard");
}

void test_empty_merged_result_fails() {
  auto result = make_valid_multi_agent_result();
  result.merged_result = "";

  const auto guard_result = validate_multi_agent_result_required_fields(result);
  assert_true(!guard_result.ok,
              "empty merged_result should fail required fields guard");
}

void test_empty_subtask_result_item_fails_boundary_guard() {
  auto result = make_valid_multi_agent_result();
  result.subtask_results = std::vector<std::string>{"worker-a: summary", ""};

  const auto guard_result = validate_multi_agent_result_boundary(result);
  assert_true(!guard_result.ok,
              "empty subtask_results item should fail boundary guard");
}

void test_agent_result_alias_is_rejected_by_shared_boundary_guard() {
  const auto guard_result =
      validate_multi_agent_result_forbidden_field("agent_result");
  assert_true(!guard_result.ok,
              "agent_result must be rejected for MultiAgentResult");
  assert_equal(
      "multi-agent result must not replace top-level agent result",
      std::string(guard_result.reason),
      "shared boundary guard should keep normalized ADR-008 rejection reason");
}

void test_compile_time_shape_does_not_reuse_agent_result_terminal_fields() {
  static_assert(!has_result_code<MultiAgentResult>::value,
                "MultiAgentResult must not expose result_code");
  static_assert(!has_response_text<MultiAgentResult>::value,
                "MultiAgentResult must not expose response_text");
  static_assert(!has_task_completed<MultiAgentResult>::value,
                "MultiAgentResult must not expose task_completed");
  static_assert(!has_error_info<MultiAgentResult>::value,
                "MultiAgentResult must not expose error_info");
}

}  // namespace

int main() {
  try {
    test_valid_multi_agent_result_passes_required_fields_guard();
    test_valid_multi_agent_result_passes_boundary_guard();
    test_missing_subtask_results_fails();
    test_empty_merged_result_fails();
    test_empty_subtask_result_item_fails_boundary_guard();
    test_agent_result_alias_is_rejected_by_shared_boundary_guard();
    test_compile_time_shape_does_not_reuse_agent_result_terminal_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}