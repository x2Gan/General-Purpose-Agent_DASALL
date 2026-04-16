#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolIR.h"
#include "tool/ToolRequest.h"
#include "validation/ToolValidator.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-validator-001"),
      .tool_call_id = std::string("tool-call-validator-001"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("  {\"command\":\"echo hi\"}  "),
      .created_at = 1710000200000,
      .goal_id = std::string("goal-validator-001"),
      .worker_task_id = std::string("worker-validator-001"),
      .timeout_ms = 800U,
      .idempotency_key = std::string("idem-validator-001"),
      .tags = std::vector<std::string>{"tool"},
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.terminal"),
      .display_name = std::string("Agent Terminal"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 3000U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

void test_validate_builds_tool_ir_and_preserves_idempotency_key() {
  const dasall::tools::validation::ToolValidator validator;
  const auto result = validator.validate(make_request(), make_descriptor());

  assert_true(result.ok(), "validate should produce a ToolIR for a valid request/descriptor pair");
  assert_equal(std::string("idem-validator-001"),
               result.tool_ir->idempotency_key.value_or(""),
               "validate should preserve the idempotency_key");
  assert_equal(std::string("{\"command\":\"echo hi\"}"),
               result.tool_ir->normalized_arguments.value_or(""),
               "validate should normalize arguments without changing their payload semantics");
  assert_true(result.tool_ir->operation == dasall::contracts::ToolIROperation::Invoke,
              "requests without explicit validation tags should remain Invoke operations");
  assert_true(result.tool_ir->route == dasall::contracts::ToolIRRoute::LocalTool,
              "action descriptors should default to the local builtin route hint");
}

void test_validate_rejects_descriptor_mismatch() {
  const dasall::tools::validation::ToolValidator validator;
  auto descriptor = make_descriptor();
  descriptor.category = dasall::contracts::ToolCategory::Information;

  const auto result = validator.validate(make_request(), descriptor);
  assert_true(!result.ok(), "validate should fail closed when invocation kind and descriptor category diverge");
  assert_equal(std::string("DescriptorMismatch"),
               result.diagnostics->error_code,
               "descriptor mismatch should report a stable validator error code");
}

void test_validate_rejects_missing_request_fields() {
  const dasall::tools::validation::ToolValidator validator;
  auto request = make_request();
  request.arguments_payload = std::nullopt;

  const auto result = validator.validate(request, make_descriptor());
  assert_true(!result.ok(), "validate should fail closed when ToolRequest guard validation fails");
  assert_equal(std::string("InvalidRequest"),
               result.diagnostics->error_code,
               "request guard failures should be surfaced as InvalidRequest");
}

}  // namespace

int main() {
  try {
    test_validate_builds_tool_ir_and_preserves_idempotency_key();
    test_validate_rejects_descriptor_mismatch();
    test_validate_rejects_missing_request_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}