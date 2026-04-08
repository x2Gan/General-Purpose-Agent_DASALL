// WP04-T003-B: PromptComposeRequest field-level contract tests.
//
// Validates the field rules enforced by
// validate_prompt_compose_request_field_rules():
//   - Optional string fields must be non-empty when present.
//   - visible_tools must be non-empty, contain no empty strings, and remain a
//     unique set of tool identifiers.
//   - tags must be non-empty and contain no empty strings when present.
//   - Illegal combinations are rejected:
//       * request_id must equal context_packet_id
//       * visible_tools must not contain duplicate identifiers
//   - All required + boundary rules from WP04-T002-B are inherited.

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptComposeRequestGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CompositionStage;
using dasall::contracts::PromptComposeRequest;
using dasall::contracts::validate_prompt_compose_request_field_rules;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

PromptComposeRequest make_valid_request() {
  PromptComposeRequest req;
  req.request_id = "req-field-003";
  req.stage = CompositionStage::Execution;
  req.context_packet_id = "req-field-003";
  req.created_at = 1710000200000;
  return req;
}

// ===========================================================================
// Positive cases
// ===========================================================================

void test_minimal_valid_request_passes_field_rules() {
  auto req = make_valid_request();
  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(result.ok,
              "minimal valid request should pass field rules");
}

void test_full_valid_request_passes_field_rules() {
  auto req = make_valid_request();
  req.task_type = "codegen";
  req.prompt_release_id = "prompt-release-v2";
  req.visible_tools = std::vector<std::string>{"read_file", "grep_search"};
  req.model_route = "gpt-4o";
  req.output_schema_ref = "schema:codegen:v1";
  req.response_format = "json_object";
  req.tags = std::vector<std::string>{"prompt", "execution"};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(result.ok,
              "full valid request should pass field rules");
}

void test_partial_optional_fields_pass() {
  auto req = make_valid_request();
  req.task_type = "summarize";
  req.model_route = "gpt-4o-mini";

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(result.ok,
              "partial optional fields should pass field rules");
}

void test_unique_visible_tools_pass() {
  auto req = make_valid_request();
  req.visible_tools = std::vector<std::string>{"tool_a", "tool_b", "tool_c"};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(result.ok,
              "unique visible_tools should pass field rules");
}

// ===========================================================================
// Negative cases: inherited required / boundary rules
// ===========================================================================

void test_missing_stage_fails_via_inherited_rules() {
  auto req = make_valid_request();
  req.stage = std::nullopt;

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "missing stage should fail via inherited required rules");
}

void test_created_at_zero_fails_via_inherited_rules() {
  auto req = make_valid_request();
  req.created_at = 0;

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "zero created_at should fail via inherited boundary rules");
}

// ===========================================================================
// Negative cases: optional string fields present but empty
// ===========================================================================

void test_empty_task_type_fails() {
  auto req = make_valid_request();
  req.task_type = "";

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok, "empty task_type should fail field rules");
  assert_equal("task_type must be non-empty when present",
               std::string(result.reason),
               "empty task_type should return canonical reason");
}

void test_empty_prompt_release_id_fails() {
  auto req = make_valid_request();
  req.prompt_release_id = "";

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok, "empty prompt_release_id should fail field rules");
}

void test_empty_output_schema_ref_fails() {
  auto req = make_valid_request();
  req.output_schema_ref = "";

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok, "empty output_schema_ref should fail field rules");
}

void test_empty_response_format_fails() {
  auto req = make_valid_request();
  req.response_format = "";

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok, "empty response_format should fail field rules");
}

// ===========================================================================
// Negative cases: vector and combination rules
// ===========================================================================

void test_empty_visible_tools_vector_fails() {
  auto req = make_valid_request();
  req.visible_tools = std::vector<std::string>{};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok, "empty visible_tools vector should fail field rules");
  assert_equal("visible_tools must contain at least one item when present",
               std::string(result.reason),
               "empty visible_tools should return canonical reason");
}

void test_visible_tools_with_empty_string_fails() {
  auto req = make_valid_request();
  req.visible_tools = std::vector<std::string>{"tool_a", ""};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "visible_tools containing empty strings should fail field rules");
  assert_equal("visible_tools must not contain empty-string elements",
               std::string(result.reason),
               "empty visible_tools element should return canonical reason");
}

void test_duplicate_visible_tools_fails() {
  auto req = make_valid_request();
  req.visible_tools = std::vector<std::string>{"tool_a", "tool_b", "tool_a"};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "duplicate visible_tools should fail field rules");
  assert_equal("visible_tools must not contain duplicate tool identifiers",
               std::string(result.reason),
               "duplicate visible_tools should return canonical reason");
}

void test_request_and_context_packet_id_mismatch_fails() {
  auto req = make_valid_request();
  req.context_packet_id = "req-other-003";

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "request/context mismatch should fail field rules");
  assert_equal("context_packet_id must equal request_id for the active request context",
               std::string(result.reason),
               "request/context mismatch should return canonical reason");
}

void test_empty_tags_vector_fails() {
  auto req = make_valid_request();
  req.tags = std::vector<std::string>{};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "empty tags vector should fail field rules");
}

void test_tags_with_empty_element_fails() {
  auto req = make_valid_request();
  req.tags = std::vector<std::string>{"prompt", ""};

  auto result = validate_prompt_compose_request_field_rules(req);
  assert_true(!result.ok,
              "tags with empty element should fail field rules");
}

}  // namespace

int main() {
  try {
    test_minimal_valid_request_passes_field_rules();
    test_full_valid_request_passes_field_rules();
    test_partial_optional_fields_pass();
    test_unique_visible_tools_pass();

    test_missing_stage_fails_via_inherited_rules();
    test_created_at_zero_fails_via_inherited_rules();
    test_empty_task_type_fails();
    test_empty_prompt_release_id_fails();
    test_empty_output_schema_ref_fails();
    test_empty_response_format_fails();
    test_empty_visible_tools_vector_fails();
    test_visible_tools_with_empty_string_fails();
    test_duplicate_visible_tools_fails();
    test_request_and_context_packet_id_mismatch_fails();
    test_empty_tags_vector_fails();
    test_tags_with_empty_element_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
