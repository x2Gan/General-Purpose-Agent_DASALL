#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "agent/MultiAgentRequest.h"
#include "agent/MultiAgentRequestGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CollaborationMode;
using dasall::contracts::MultiAgentRequest;
using dasall::contracts::validate_multi_agent_request_boundary;
using dasall::contracts::validate_multi_agent_request_forbidden_field;
using dasall::contracts::validate_multi_agent_request_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

template <typename T, typename = void>
struct has_user_input : std::false_type {};

template <typename T>
struct has_user_input<T, std::void_t<decltype(std::declval<T>().user_input)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_request_channel : std::false_type {};

template <typename T>
struct has_request_channel<
    T,
    std::void_t<decltype(std::declval<T>().request_channel)>> : std::true_type {
};

template <typename T, typename = void>
struct has_domain_context : std::false_type {};

template <typename T>
struct has_domain_context<
    T,
    std::void_t<decltype(std::declval<T>().domain_context)>> : std::true_type {
};

template <typename T, typename = void>
struct has_runtime_budget : std::false_type {};

template <typename T>
struct has_runtime_budget<
    T,
    std::void_t<decltype(std::declval<T>().runtime_budget)>> : std::true_type {
};

MultiAgentRequest make_valid_multi_agent_request() {
  MultiAgentRequest request;
  request.parent_request_id = "req-001";
  request.parent_task_id = "task-parent-001";
  request.goal_fragment = "collect evidence from specialist workers";
  request.plan_fragment = "dispatch analysis and merge intermediate results";
  request.collaboration_mode = CollaborationMode::Concurrent;
  request.permission_guard = "read-only-analysis";
  request.stop_conditions = std::vector<std::string>{"all-subtasks-complete"};
  return request;
}

void test_valid_multi_agent_request_passes_required_fields_guard() {
  const auto request = make_valid_multi_agent_request();
  const auto result = validate_multi_agent_request_required_fields(request);
  assert_true(result.ok,
              "valid multi-agent request should pass required fields guard");
}

void test_valid_multi_agent_request_passes_boundary_guard() {
  const auto request = make_valid_multi_agent_request();
  const auto result = validate_multi_agent_request_boundary(request);
  assert_true(result.ok,
              "valid multi-agent request should pass boundary guard");
}

void test_missing_parent_request_id_fails() {
  auto request = make_valid_multi_agent_request();
  request.parent_request_id = std::nullopt;
  const auto result = validate_multi_agent_request_required_fields(request);
  assert_true(!result.ok,
              "missing parent_request_id should fail required fields guard");
}

void test_unspecified_collaboration_mode_fails() {
  auto request = make_valid_multi_agent_request();
  request.collaboration_mode = CollaborationMode::Unspecified;
  const auto result = validate_multi_agent_request_required_fields(request);
  assert_true(!result.ok,
              "Unspecified collaboration_mode should fail required fields guard");
}

void test_parent_request_and_parent_task_anchor_collapse_fails() {
  auto request = make_valid_multi_agent_request();
  request.parent_task_id = request.parent_request_id;
  const auto result = validate_multi_agent_request_boundary(request);
  assert_true(!result.ok,
              "request/task anchor collapse should fail boundary guard");
  assert_equal(
      "parent_request_id must not equal parent_task_id because request and task anchors are layered separately",
      std::string(result.reason),
      "anchor collapse should return a precise boundary violation reason");
}

void test_agent_request_alias_is_rejected_by_shared_boundary_guard() {
  const auto result = validate_multi_agent_request_forbidden_field(
      "agent_request_payload");
  assert_true(!result.ok,
              "agent_request_payload must be rejected for MultiAgentRequest");
  assert_equal(
      "multi-agent request must not reuse agent-request semantics",
      std::string(result.reason),
      "shared boundary guard should keep normalized ADR-008 rejection reason");
}

void test_compile_time_shape_does_not_reuse_agent_request_entry_fields() {
  static_assert(!has_user_input<MultiAgentRequest>::value,
                "MultiAgentRequest must not expose user_input");
  static_assert(!has_request_channel<MultiAgentRequest>::value,
                "MultiAgentRequest must not expose request_channel");
  static_assert(!has_domain_context<MultiAgentRequest>::value,
                "MultiAgentRequest must not expose domain_context");
  static_assert(!has_runtime_budget<MultiAgentRequest>::value,
                "MultiAgentRequest must not expose runtime_budget");
}

}  // namespace

int main() {
  try {
    test_valid_multi_agent_request_passes_required_fields_guard();
    test_valid_multi_agent_request_passes_boundary_guard();
    test_missing_parent_request_id_fails();
    test_unspecified_collaboration_mode_fails();
    test_parent_request_and_parent_task_anchor_collapse_fails();
    test_agent_request_alias_is_rejected_by_shared_boundary_guard();
    test_compile_time_shape_does_not_reuse_agent_request_entry_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}