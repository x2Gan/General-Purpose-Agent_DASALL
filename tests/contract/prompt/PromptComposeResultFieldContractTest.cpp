// WP04-T005-B: PromptComposeResult field-level contract tests.
//
// Validates the optional metadata field rules enforced by
// validate_prompt_compose_result_field_rules():
//   - T004 required/boundary rules are inherited.
//   - pruned_sections must be non-empty, contain no empty strings, and remain
//     a unique list of section identifiers.
//   - composition_warnings must be non-empty and contain no empty strings.

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "dasall/tests/support/TestAssertions.h"
#include "prompt/PromptComposeResult.h"
#include "prompt/PromptComposeResultGuards.h"

namespace {

using dasall::contracts::PromptComposeResult;
using dasall::contracts::validate_prompt_compose_result_field_rules;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

PromptComposeResult make_valid_result() {
  PromptComposeResult result;
  result.messages = std::vector<std::string>{"system:plan", "user:task"};
  result.selected_prompt_id = "prompt.plan.default";
  result.selected_version = "2026-03-17.1";
  result.estimated_tokens = 128;
  return result;
}

void test_minimal_valid_result_passes_field_rules() {
  const auto result = make_valid_result();
  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(guard.ok,
              "minimal valid PromptComposeResult must pass field rules");
}

void test_full_valid_result_passes_field_rules() {
  auto result = make_valid_result();
  result.pruned_sections = std::vector<std::string>{"history", "examples"};
  result.composition_warnings = std::vector<std::string>{"budget_near_limit", "fallback_template_used"};

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(guard.ok,
              "full valid PromptComposeResult must pass field rules");
}

void test_missing_messages_fails_via_inherited_rules() {
  auto result = make_valid_result();
  result.messages = std::nullopt;

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "missing messages must fail via inherited required rules");
}

void test_negative_estimated_tokens_fails_via_inherited_rules() {
  auto result = make_valid_result();
  result.estimated_tokens = -1;

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "negative estimated_tokens must fail via inherited required rules");
}

void test_empty_pruned_sections_vector_fails() {
  auto result = make_valid_result();
  result.pruned_sections = std::vector<std::string>{};

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "empty pruned_sections vector must fail field rules");
  assert_equal("pruned_sections must contain at least one item when present",
               std::string(guard.reason),
               "empty pruned_sections must return canonical reason");
}

void test_pruned_sections_with_empty_element_fails() {
  auto result = make_valid_result();
  result.pruned_sections = std::vector<std::string>{"history", ""};

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "pruned_sections with empty element must fail field rules");
  assert_equal("pruned_sections must not contain empty-string elements",
               std::string(guard.reason),
               "empty pruned_sections element must return canonical reason");
}

void test_duplicate_pruned_sections_fails() {
  auto result = make_valid_result();
  result.pruned_sections = std::vector<std::string>{"history", "examples", "history"};

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "duplicate pruned_sections must fail field rules");
  assert_equal("pruned_sections must not contain duplicate section identifiers",
               std::string(guard.reason),
               "duplicate pruned_sections must return canonical reason");
}

void test_empty_composition_warnings_vector_fails() {
  auto result = make_valid_result();
  result.composition_warnings = std::vector<std::string>{};

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "empty composition_warnings vector must fail field rules");
  assert_equal("composition_warnings must contain at least one item when present",
               std::string(guard.reason),
               "empty composition_warnings must return canonical reason");
}

void test_composition_warnings_with_empty_element_fails() {
  auto result = make_valid_result();
  result.composition_warnings = std::vector<std::string>{"budget_near_limit", ""};

  const auto guard = validate_prompt_compose_result_field_rules(result);
  assert_true(!guard.ok,
              "composition_warnings with empty element must fail field rules");
  assert_equal("composition_warnings must not contain empty-string elements",
               std::string(guard.reason),
               "empty composition_warnings element must return canonical reason");
}

}  // namespace

int main() {
  try {
    test_minimal_valid_result_passes_field_rules();
    test_full_valid_result_passes_field_rules();
    test_missing_messages_fails_via_inherited_rules();
    test_negative_estimated_tokens_fails_via_inherited_rules();
    test_empty_pruned_sections_vector_fails();
    test_pruned_sections_with_empty_element_fails();
    test_duplicate_pruned_sections_fails();
    test_empty_composition_warnings_vector_fails();
    test_composition_warnings_with_empty_element_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}