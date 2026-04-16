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
      .request_id = std::string("req-validator-dryrun"),
      .tool_call_id = std::string("tool-call-validator-dryrun"),
      .tool_name = std::string("agent.diagnose"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Diagnostic,
      .arguments_payload = std::string("{\"target\":\"lane\"}"),
      .created_at = 1710000200000,
      .tags = std::vector<std::string>{"tool"},
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.diagnose"),
      .display_name = std::string("Agent Diagnose"),
      .category = dasall::contracts::ToolCategory::Diagnostic,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 1200U,
      .input_schema_ref = std::string("schema://tools/agent.diagnose/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.diagnose/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.diagnose"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

void test_dry_run_tag_maps_to_non_executing_operation() {
  const dasall::tools::validation::ToolValidator validator;
  auto request = make_request();
  request.tags = std::vector<std::string>{"tool", "tool.mode.dry_run"};

  const auto result = validator.validate(request, make_descriptor());
  assert_true(result.ok(), "tool.mode.dry_run should remain a valid non-executing validator branch");
  assert_true(result.tool_ir->operation == dasall::contracts::ToolIROperation::DryRun,
              "tool.mode.dry_run should map to ToolIROperation::DryRun");
}

void test_validate_only_tag_maps_to_non_executing_operation() {
  const dasall::tools::validation::ToolValidator validator;
  auto request = make_request();
  request.tags = std::vector<std::string>{"tool", "tool.mode.validate_only"};

  const auto result = validator.validate(request, make_descriptor());
  assert_true(result.ok(), "tool.mode.validate_only should remain a valid non-executing validator branch");
  assert_true(result.tool_ir->operation == dasall::contracts::ToolIROperation::ValidateOnly,
              "tool.mode.validate_only should map to ToolIROperation::ValidateOnly");
}

void test_conflicting_validation_tags_are_rejected() {
  const dasall::tools::validation::ToolValidator validator;
  auto request = make_request();
  request.tags = std::vector<std::string>{
      "tool",
      "tool.mode.dry_run",
      "tool.mode.validate_only",
  };

  const auto result = validator.validate(request, make_descriptor());
  assert_true(!result.ok(), "conflicting validation mode tags must be rejected instead of guessed");
  assert_equal(std::string("InvalidValidationMode"),
               result.diagnostics->error_code,
               "conflicting validation tags should surface a stable validator error code");
}

}  // namespace

int main() {
  try {
    test_dry_run_tag_maps_to_non_executing_operation();
    test_validate_only_tag_maps_to_non_executing_operation();
    test_conflicting_validation_tags_are_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}