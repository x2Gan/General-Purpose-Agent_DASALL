#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"
#include "dasall/tests/support/TestAssertions.h"
#include "tool/ToolRequest.h"
#include "tool/ToolRequestGuards.h"

namespace {

using dasall::contracts::RuntimeBudget;
using dasall::contracts::ToolInvocationKind;
using dasall::contracts::ToolRequest;
using dasall::contracts::ToolRequestBoundaryDecision;
using dasall::contracts::evaluate_tool_request_field_boundary;
using dasall::contracts::validate_tool_request_boundary;
using dasall::contracts::validate_tool_request_field_rules;
using dasall::contracts::validate_tool_request_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ToolRequest make_valid_request() {
  ToolRequest request;
  request.request_id = "req-005-001";
  request.tool_call_id = "tool-call-001";
  request.tool_name = "knowledge_search";
  request.invocation_kind = ToolInvocationKind::InformationQuery;
  request.arguments_payload = R"({"query":"tool request contract"})";
  request.created_at = 1710000200000;
  return request;
}

ToolRequest make_full_request() {
  auto request = make_valid_request();
  request.goal_id = "goal-001";
  request.worker_task_id = "worker-task-002";
  request.runtime_budget = RuntimeBudget{
      .max_tokens = 512,
      .max_turns = 2,
      .max_tool_calls = 1,
      .max_latency_ms = 5000,
      .max_replan_count = 1,
  };
  request.timeout_ms = 3000;
  request.idempotency_key = "idem-001";
  request.tags = std::vector<std::string>{"tool", "contract"};
  return request;
}

void test_valid_minimal_request_passes_required_fields() {
  const auto result = validate_tool_request_required_fields(make_valid_request());
  assert_true(result.ok, "minimal valid ToolRequest should pass required-field validation");
}

void test_valid_minimal_request_passes_boundary() {
  const auto result = validate_tool_request_boundary(make_valid_request());
  assert_true(result.ok, "minimal valid ToolRequest should pass boundary validation");
}

void test_valid_full_request_passes_field_rules() {
  const auto result = validate_tool_request_field_rules(make_full_request());
  assert_true(result.ok, "full valid ToolRequest should pass field-rule validation");
}

void test_all_invocation_kinds_are_accepted() {
  auto request = make_valid_request();

  request.invocation_kind = ToolInvocationKind::InformationQuery;
  assert_true(validate_tool_request_boundary(request).ok,
              "InformationQuery should be accepted");

  request.invocation_kind = ToolInvocationKind::Action;
  assert_true(validate_tool_request_boundary(request).ok,
              "Action should be accepted");

  request.invocation_kind = ToolInvocationKind::Workflow;
  assert_true(validate_tool_request_boundary(request).ok,
              "Workflow should be accepted");

  request.invocation_kind = ToolInvocationKind::AgentDelegation;
  assert_true(validate_tool_request_boundary(request).ok,
              "AgentDelegation should be accepted");

  request.invocation_kind = ToolInvocationKind::Diagnostic;
  assert_true(validate_tool_request_boundary(request).ok,
              "Diagnostic should be accepted");
}

void test_missing_request_id_is_rejected() {
  auto request = make_valid_request();
  request.request_id = std::nullopt;
  const auto result = validate_tool_request_required_fields(request);

  assert_true(!result.ok, "missing request_id must be rejected");
  assert_equal("request_id is required and must be non-empty",
               std::string(result.reason),
               "missing request_id should report the correct reason");
}

void test_missing_tool_call_id_is_rejected() {
  auto request = make_valid_request();
  request.tool_call_id = std::nullopt;
  const auto result = validate_tool_request_required_fields(request);

  assert_true(!result.ok, "missing tool_call_id must be rejected");
}

void test_missing_tool_name_is_rejected() {
  auto request = make_valid_request();
  request.tool_name = std::nullopt;
  const auto result = validate_tool_request_required_fields(request);

  assert_true(!result.ok, "missing tool_name must be rejected");
}

void test_unspecified_invocation_kind_is_rejected() {
  auto request = make_valid_request();
  request.invocation_kind = ToolInvocationKind::Unspecified;
  const auto result = validate_tool_request_required_fields(request);

  assert_true(!result.ok, "Unspecified invocation_kind must be rejected");
}

void test_missing_arguments_payload_is_rejected() {
  auto request = make_valid_request();
  request.arguments_payload = std::nullopt;
  const auto result = validate_tool_request_required_fields(request);

  assert_true(!result.ok, "missing arguments_payload must be rejected");
}

void test_missing_created_at_is_rejected() {
  auto request = make_valid_request();
  request.created_at = std::nullopt;
  const auto result = validate_tool_request_required_fields(request);

  assert_true(!result.ok, "missing created_at must be rejected");
}

void test_out_of_range_invocation_kind_is_rejected() {
  auto request = make_valid_request();
  request.invocation_kind = static_cast<ToolInvocationKind>(99);
  const auto result = validate_tool_request_boundary(request);

  assert_true(!result.ok, "out-of-range invocation_kind must be rejected");
  assert_equal("invocation_kind value is outside the known enum range",
               std::string(result.reason),
               "out-of-range invocation_kind should report the correct reason");
}

void test_tool_call_id_equal_to_request_id_is_rejected() {
  auto request = make_valid_request();
  request.tool_call_id = *request.request_id;
  const auto result = validate_tool_request_boundary(request);

  assert_true(!result.ok, "tool_call_id equal to request_id must be rejected");
}

void test_empty_goal_id_is_rejected() {
  auto request = make_valid_request();
  request.goal_id = "";
  const auto result = validate_tool_request_field_rules(request);

  assert_true(!result.ok, "empty goal_id must be rejected");
  assert_equal("goal_id must be non-empty when present",
               std::string(result.reason),
               "empty goal_id should report the correct reason");
}

void test_zero_timeout_is_rejected() {
  auto request = make_valid_request();
  request.timeout_ms = 0;
  const auto result = validate_tool_request_field_rules(request);

  assert_true(!result.ok, "zero timeout_ms must be rejected");
}

void test_duplicate_tags_are_rejected() {
  auto request = make_valid_request();
  request.tags = std::vector<std::string>{"tool", "tool"};
  const auto result = validate_tool_request_field_rules(request);

  assert_true(!result.ok, "duplicate tags must be rejected");
  assert_equal("tags must not contain duplicate items",
               std::string(result.reason),
               "duplicate tags should report the correct reason");
}

void test_zero_budget_dimension_is_rejected() {
  auto request = make_valid_request();
  request.runtime_budget = RuntimeBudget{
      .max_tokens = 0,
      .max_turns = 1,
      .max_tool_calls = 1,
      .max_latency_ms = 1000,
      .max_replan_count = 1,
  };
  const auto result = validate_tool_request_field_rules(request);

  assert_true(!result.ok, "zero runtime budget dimensions must be rejected");
}

void test_execution_result_field_is_rejected() {
  const auto result = evaluate_tool_request_field_boundary("observation");

  assert_true(!result.allowed, "observation must be rejected for ToolRequest");
  assert_equal(static_cast<int>(ToolRequestBoundaryDecision::RejectExecutionResultField),
               static_cast<int>(result.decision),
               "observation should map to RejectExecutionResultField");
}

void test_prompt_provider_field_is_rejected() {
  const auto result = evaluate_tool_request_field_boundary("rendered_prompt");

  assert_true(!result.allowed, "rendered_prompt must be rejected for ToolRequest");
  assert_equal(static_cast<int>(ToolRequestBoundaryDecision::RejectPromptProviderField),
               static_cast<int>(result.decision),
               "rendered_prompt should map to RejectPromptProviderField");
}

void test_descriptor_field_is_rejected() {
  const auto result = evaluate_tool_request_field_boundary("tool_schema");

  assert_true(!result.allowed, "tool_schema must be rejected for ToolRequest");
  assert_equal(static_cast<int>(ToolRequestBoundaryDecision::RejectDescriptorOwnershipField),
               static_cast<int>(result.decision),
               "tool_schema should map to RejectDescriptorOwnershipField");
}

void test_regular_field_name_is_allowed() {
  const auto result = evaluate_tool_request_field_boundary("tool_name");
  assert_true(result.allowed, "tool_name should remain allowed in ToolRequest");
}

}  // namespace

int main() {
  try {
    test_valid_minimal_request_passes_required_fields();
    test_valid_minimal_request_passes_boundary();
    test_valid_full_request_passes_field_rules();
    test_all_invocation_kinds_are_accepted();
    test_missing_request_id_is_rejected();
    test_missing_tool_call_id_is_rejected();
    test_missing_tool_name_is_rejected();
    test_unspecified_invocation_kind_is_rejected();
    test_missing_arguments_payload_is_rejected();
    test_missing_created_at_is_rejected();
    test_out_of_range_invocation_kind_is_rejected();
    test_tool_call_id_equal_to_request_id_is_rejected();
    test_empty_goal_id_is_rejected();
    test_zero_timeout_is_rejected();
    test_duplicate_tags_are_rejected();
    test_zero_budget_dimension_is_rejected();
    test_execution_result_field_is_rejected();
    test_prompt_provider_field_is_rejected();
    test_descriptor_field_is_rejected();
    test_regular_field_name_is_allowed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}