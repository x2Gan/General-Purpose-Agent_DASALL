#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "error/ResultCode.h"
#include "tool/ToolResult.h"
#include "tool/ToolResultGuards.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::ToolResult;
using dasall::contracts::ToolResultBoundaryDecision;
using dasall::contracts::evaluate_tool_result_field_boundary;
using dasall::contracts::validate_tool_result_boundary;
using dasall::contracts::validate_tool_result_field_rules;
using dasall::contracts::validate_tool_result_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ErrorInfo make_tool_error() {
  return ErrorInfo{
      .failure_type = ResultCodeCategory::Tool,
      .retryable = false,
      .safe_to_replan = true,
      .details = ErrorDetails{
          .code = 3001,
          .message = "tool execution failed",
          .stage = "execute",
      },
      .source_ref = ErrorSourceRefMinimal{
          .ref_type = "tool_call",
          .ref_id = "tool-call-003",
      },
  };
}

ToolResult make_success_result() {
  ToolResult result;
  result.request_id = "req-005-003";
  result.tool_call_id = "tool-call-003";
  result.tool_name = "knowledge_search";
  result.success = true;
  result.payload = R"({"hits":2,"top_result":"ADR-006"})";
  result.completed_at = 1710000300000;
  result.duration_ms = 120;
  return result;
}

ToolResult make_failure_result() {
  auto result = make_success_result();
  result.success = false;
  result.payload = std::nullopt;
  result.error = make_tool_error();
  result.side_effects = std::vector<std::string>{"cache_write:tool-call-003"};
  return result;
}

ToolResult make_full_success_result() {
  auto result = make_success_result();
  result.goal_id = "goal-003";
  result.worker_task_id = "worker-task-003";
  result.tags = std::vector<std::string>{"tool", "contract"};
  return result;
}

void test_valid_success_result_passes_required_fields() {
  const auto validation = validate_tool_result_required_fields(make_success_result());
  assert_true(validation.ok, "minimal successful ToolResult should pass required-field validation");
}

void test_valid_success_result_passes_boundary() {
  const auto validation = validate_tool_result_boundary(make_success_result());
  assert_true(validation.ok, "minimal successful ToolResult should pass boundary validation");
}

void test_valid_failure_result_passes_boundary() {
  const auto validation = validate_tool_result_boundary(make_failure_result());
  assert_true(validation.ok, "failed ToolResult with ErrorInfo should still pass boundary validation");
}

void test_valid_full_success_result_passes_field_rules() {
  const auto validation = validate_tool_result_field_rules(make_full_success_result());
  assert_true(validation.ok, "full successful ToolResult should pass field-rule validation");
}

void test_missing_tool_call_id_is_rejected() {
  auto result = make_success_result();
  result.tool_call_id = std::nullopt;
  const auto validation = validate_tool_result_required_fields(result);

  assert_true(!validation.ok, "missing tool_call_id must be rejected");
}

void test_missing_success_flag_is_rejected() {
  auto result = make_success_result();
  result.success = std::nullopt;
  const auto validation = validate_tool_result_required_fields(result);

  assert_true(!validation.ok, "missing success must be rejected");
}

void test_success_without_payload_is_rejected() {
  auto result = make_success_result();
  result.payload = std::nullopt;
  const auto validation = validate_tool_result_boundary(result);

  assert_true(!validation.ok, "success=true without payload must be rejected");
  assert_equal("payload is required and must be non-empty when success is true",
               std::string(validation.reason),
               "success result without payload should report the correct reason");
}

void test_failure_without_error_is_rejected() {
  auto result = make_success_result();
  result.success = false;
  result.payload = std::nullopt;
  const auto validation = validate_tool_result_boundary(result);

  assert_true(!validation.ok, "success=false without error must be rejected");
}

void test_success_with_error_is_rejected() {
  auto result = make_success_result();
  result.error = make_tool_error();
  const auto validation = validate_tool_result_boundary(result);

  assert_true(!validation.ok, "success=true with error must be rejected");
}

void test_tool_call_id_equal_to_request_id_is_rejected() {
  auto result = make_success_result();
  result.tool_call_id = *result.request_id;
  const auto validation = validate_tool_result_boundary(result);

  assert_true(!validation.ok, "tool_call_id equal to request_id must be rejected");
}

void test_zero_duration_is_rejected() {
  auto result = make_success_result();
  result.duration_ms = 0;
  const auto validation = validate_tool_result_boundary(result);

  assert_true(!validation.ok, "zero duration_ms must be rejected");
}

void test_duplicate_side_effects_are_rejected() {
  auto result = make_failure_result();
  result.side_effects = std::vector<std::string>{"write-audit", "write-audit"};
  const auto validation = validate_tool_result_field_rules(result);

  assert_true(!validation.ok, "duplicate side_effects must be rejected");
  assert_equal("side_effects must not contain duplicate items",
               std::string(validation.reason),
               "duplicate side_effects should report the correct reason");
}

void test_empty_tags_are_rejected() {
  auto result = make_success_result();
  result.tags = std::vector<std::string>{};
  const auto validation = validate_tool_result_field_rules(result);

  assert_true(!validation.ok, "empty tags vector must be rejected");
}

void test_observation_field_is_rejected() {
  const auto validation = evaluate_tool_result_field_boundary("observation");

  assert_true(!validation.allowed, "observation must be rejected for ToolResult");
  assert_equal(
      static_cast<int>(ToolResultBoundaryDecision::RejectObservationOwnershipField),
      static_cast<int>(validation.decision),
      "observation should map to RejectObservationOwnershipField");
}

void test_runtime_accounting_field_is_rejected() {
  const auto validation = evaluate_tool_result_field_boundary("spent_tokens");

  assert_true(!validation.allowed, "spent_tokens must be rejected for ToolResult");
  assert_equal(
      static_cast<int>(ToolResultBoundaryDecision::RejectRuntimeAccountingField),
      static_cast<int>(validation.decision),
      "spent_tokens should map to RejectRuntimeAccountingField");
}

void test_recovery_control_field_is_rejected() {
  const auto validation = evaluate_tool_result_field_boundary("checkpoint_ref");

  assert_true(!validation.allowed, "checkpoint_ref must be rejected for ToolResult");
  assert_equal(
      static_cast<int>(ToolResultBoundaryDecision::RejectRecoveryControlField),
      static_cast<int>(validation.decision),
      "checkpoint_ref should map to RejectRecoveryControlField");
}

void test_regular_field_name_is_allowed() {
  const auto validation = evaluate_tool_result_field_boundary("payload");
  assert_true(validation.allowed, "payload should remain allowed in ToolResult");
}

}  // namespace

int main() {
  try {
    test_valid_success_result_passes_required_fields();
    test_valid_success_result_passes_boundary();
    test_valid_failure_result_passes_boundary();
    test_valid_full_success_result_passes_field_rules();
    test_missing_tool_call_id_is_rejected();
    test_missing_success_flag_is_rejected();
    test_success_without_payload_is_rejected();
    test_failure_without_error_is_rejected();
    test_success_with_error_is_rejected();
    test_tool_call_id_equal_to_request_id_is_rejected();
    test_zero_duration_is_rejected();
    test_duplicate_side_effects_are_rejected();
    test_empty_tags_are_rejected();
    test_observation_field_is_rejected();
    test_runtime_accounting_field_is_rejected();
    test_recovery_control_field_is_rejected();
    test_regular_field_name_is_allowed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}