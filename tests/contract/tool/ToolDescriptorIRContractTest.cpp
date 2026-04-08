#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"

namespace {

using dasall::contracts::ToolCategory;
using dasall::contracts::ToolCapabilityTier;
using dasall::contracts::ToolDescriptor;
using dasall::contracts::ToolDescriptorBoundaryDecision;
using dasall::contracts::ToolIR;
using dasall::contracts::ToolIRBoundaryDecision;
using dasall::contracts::ToolIROperation;
using dasall::contracts::ToolIRPriority;
using dasall::contracts::ToolIRRoute;
using dasall::contracts::evaluate_tool_descriptor_field_boundary;
using dasall::contracts::evaluate_tool_ir_field_boundary;
using dasall::contracts::validate_tool_descriptor_field_rules;
using dasall::contracts::validate_tool_descriptor_required_fields;
using dasall::contracts::validate_tool_ir_field_rules;
using dasall::contracts::validate_tool_ir_required_fields;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

ToolDescriptor make_valid_descriptor() {
  ToolDescriptor descriptor;
  descriptor.tool_name = "knowledge_search";
  descriptor.display_name = "Knowledge Search";
  descriptor.category = ToolCategory::Information;
  descriptor.capability_tier = ToolCapabilityTier::Stable;
  descriptor.is_read_only = true;
  descriptor.supports_compensation = false;
  descriptor.default_timeout_ms = 3000;
  descriptor.input_schema_ref = "schema://tool/knowledge_search/request/v1";
  descriptor.output_schema_ref = "schema://tool/knowledge_search/response/v1";
  descriptor.required_scopes = std::vector<std::string>{"knowledge:read"};
  descriptor.tags = std::vector<std::string>{"tool", "search"};
  descriptor.version = "v1";
  return descriptor;
}

ToolIR make_valid_tool_ir() {
  ToolIR tool_ir;
  tool_ir.request_id = "req-005-004";
  tool_ir.tool_call_id = "tool-call-004";
  tool_ir.tool_name = "knowledge_search";
  tool_ir.operation = ToolIROperation::Invoke;
  tool_ir.normalized_arguments = R"({"query":"tool descriptor"})";
  tool_ir.route = ToolIRRoute::LocalTool;
  tool_ir.timeout_ms = 2500;
  tool_ir.idempotency_key = "idem-004";
  tool_ir.priority = ToolIRPriority::Normal;
  tool_ir.goal_id = "goal-004";
  return tool_ir;
}

void test_valid_descriptor_passes_required_fields() {
  const auto result = validate_tool_descriptor_required_fields(make_valid_descriptor());
  assert_true(result.ok, "valid ToolDescriptor should pass required-field validation");
}

void test_valid_descriptor_passes_field_rules() {
  const auto result = validate_tool_descriptor_field_rules(make_valid_descriptor());
  assert_true(result.ok, "valid ToolDescriptor should pass field-rule validation");
}

void test_descriptor_missing_tool_name_is_rejected() {
  auto descriptor = make_valid_descriptor();
  descriptor.tool_name = std::nullopt;
  const auto result = validate_tool_descriptor_required_fields(descriptor);

  assert_true(!result.ok, "missing tool_name must be rejected");
  assert_equal("tool_name is required and must be non-empty",
               std::string(result.reason),
               "missing tool_name should report the correct reason");
}

void test_descriptor_out_of_range_category_is_rejected() {
  auto descriptor = make_valid_descriptor();
  descriptor.category = static_cast<ToolCategory>(99);
  const auto result = validate_tool_descriptor_field_rules(descriptor);

  assert_true(!result.ok, "out-of-range category must be rejected");
}

void test_descriptor_duplicate_scopes_are_rejected() {
  auto descriptor = make_valid_descriptor();
  descriptor.required_scopes = std::vector<std::string>{"knowledge:read", "knowledge:read"};
  const auto result = validate_tool_descriptor_field_rules(descriptor);

  assert_true(!result.ok, "duplicate required_scopes must be rejected");
  assert_equal("required_scopes must not contain duplicate items",
               std::string(result.reason),
               "duplicate required_scopes should report the correct reason");
}

void test_valid_tool_ir_passes_required_fields() {
  const auto result = validate_tool_ir_required_fields(make_valid_tool_ir());
  assert_true(result.ok, "valid ToolIR should pass required-field validation");
}

void test_valid_tool_ir_passes_field_rules() {
  const auto result = validate_tool_ir_field_rules(make_valid_tool_ir());
  assert_true(result.ok, "valid ToolIR should pass field-rule validation");
}

void test_tool_ir_missing_tool_call_id_is_rejected() {
  auto tool_ir = make_valid_tool_ir();
  tool_ir.tool_call_id = std::nullopt;
  const auto result = validate_tool_ir_required_fields(tool_ir);

  assert_true(!result.ok, "missing tool_call_id must be rejected");
}

void test_tool_ir_out_of_range_operation_is_rejected() {
  auto tool_ir = make_valid_tool_ir();
  tool_ir.operation = static_cast<ToolIROperation>(99);
  const auto result = validate_tool_ir_field_rules(tool_ir);

  assert_true(!result.ok, "out-of-range operation must be rejected");
}

void test_tool_ir_equal_call_and_request_id_is_rejected() {
  auto tool_ir = make_valid_tool_ir();
  tool_ir.tool_call_id = *tool_ir.request_id;
  const auto result = validate_tool_ir_field_rules(tool_ir);

  assert_true(!result.ok, "tool_call_id equal to request_id must be rejected");
}

void test_descriptor_rejects_runtime_field() {
  const auto result = evaluate_tool_descriptor_field_boundary("request_id");

  assert_true(!result.allowed, "request_id must be rejected for ToolDescriptor");
  assert_equal(static_cast<int>(ToolDescriptorBoundaryDecision::RejectInvocationRuntimeField),
               static_cast<int>(result.decision),
               "request_id should map to RejectInvocationRuntimeField");
}

void test_descriptor_rejects_result_field() {
  const auto result = evaluate_tool_descriptor_field_boundary("payload");

  assert_true(!result.allowed, "payload must be rejected for ToolDescriptor");
  assert_equal(static_cast<int>(ToolDescriptorBoundaryDecision::RejectExecutionResultField),
               static_cast<int>(result.decision),
               "payload should map to RejectExecutionResultField");
}

void test_tool_ir_rejects_descriptor_catalog_field() {
  const auto result = evaluate_tool_ir_field_boundary("required_scopes");

  assert_true(!result.allowed, "required_scopes must be rejected for ToolIR");
  assert_equal(static_cast<int>(ToolIRBoundaryDecision::RejectDescriptorCatalogField),
               static_cast<int>(result.decision),
               "required_scopes should map to RejectDescriptorCatalogField");
}

void test_tool_ir_rejects_prompt_provider_field() {
  const auto result = evaluate_tool_ir_field_boundary("rendered_prompt");

  assert_true(!result.allowed, "rendered_prompt must be rejected for ToolIR");
  assert_equal(static_cast<int>(ToolIRBoundaryDecision::RejectPromptProviderField),
               static_cast<int>(result.decision),
               "rendered_prompt should map to RejectPromptProviderField");
}

void test_regular_fields_remain_allowed() {
  const auto descriptor_result = evaluate_tool_descriptor_field_boundary("tool_name");
  assert_true(descriptor_result.allowed, "tool_name should remain allowed in ToolDescriptor");

  const auto tool_ir_result = evaluate_tool_ir_field_boundary("normalized_arguments");
  assert_true(tool_ir_result.allowed, "normalized_arguments should remain allowed in ToolIR");
}

}  // namespace

int main() {
  try {
    test_valid_descriptor_passes_required_fields();
    test_valid_descriptor_passes_field_rules();
    test_descriptor_missing_tool_name_is_rejected();
    test_descriptor_out_of_range_category_is_rejected();
    test_descriptor_duplicate_scopes_are_rejected();
    test_valid_tool_ir_passes_required_fields();
    test_valid_tool_ir_passes_field_rules();
    test_tool_ir_missing_tool_call_id_is_rejected();
    test_tool_ir_out_of_range_operation_is_rejected();
    test_tool_ir_equal_call_and_request_id_is_rejected();
    test_descriptor_rejects_runtime_field();
    test_descriptor_rejects_result_field();
    test_tool_ir_rejects_descriptor_catalog_field();
    test_tool_ir_rejects_prompt_provider_field();
    test_regular_fields_remain_allowed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
