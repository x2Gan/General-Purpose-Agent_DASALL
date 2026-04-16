#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolRequest.h"
#include "validation/ToolValidator.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-validator-defaulting"),
      .tool_call_id = std::string("tool-call-validator-defaulting"),
      .tool_name = std::string("knowledge.search"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::InformationQuery,
      .arguments_payload = std::string("{\"query\":\"tools\"}"),
      .created_at = 1710000200000,
      .tags = std::vector<std::string>{"tool"},
  };
}

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("knowledge.search"),
      .display_name = std::string("Knowledge Search"),
      .category = dasall::contracts::ToolCategory::Information,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://tools/knowledge.search/input/v1"),
      .output_schema_ref = std::string("schema://tools/knowledge.search/output/v1"),
      .required_scopes = std::vector<std::string>{"knowledge.read"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

void test_descriptor_default_timeout_is_injected_when_request_omits_one() {
  const dasall::tools::validation::ToolValidator validator;
  const auto result = validator.validate(make_request(), make_descriptor());

  assert_true(result.ok(), "validate should succeed when timeout is derived from the descriptor default");
  assert_equal(2500, static_cast<int>(result.tool_ir->timeout_ms.value_or(0U)),
               "validate should inject descriptor.default_timeout_ms when request.timeout_ms is absent");
}

void test_explicit_request_timeout_overrides_descriptor_default() {
  const dasall::tools::validation::ToolValidator validator;
  auto request = make_request();
  request.timeout_ms = 900U;

  const auto result = validator.validate(request, make_descriptor());

  assert_true(result.ok(), "validate should still succeed when request provides an explicit timeout");
  assert_equal(900, static_cast<int>(result.tool_ir->timeout_ms.value_or(0U)),
               "request.timeout_ms should override descriptor.default_timeout_ms");
}

void test_missing_timeout_stays_unset_when_no_explicit_default_exists() {
  const dasall::tools::validation::ToolValidator validator;
  auto descriptor = make_descriptor();
  descriptor.default_timeout_ms = std::nullopt;

  const auto result = validator.validate(make_request(), descriptor);

  assert_true(result.ok(), "validate should not invent a timeout when neither request nor descriptor provides one");
  assert_true(!result.tool_ir->timeout_ms.has_value(),
              "timeout should remain unset instead of being silently guessed");
}

}  // namespace

int main() {
  try {
    test_descriptor_default_timeout_is_injected_when_request_omits_one();
    test_explicit_request_timeout_overrides_descriptor_default();
    test_missing_timeout_stays_unset_when_no_explicit_default_exists();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}