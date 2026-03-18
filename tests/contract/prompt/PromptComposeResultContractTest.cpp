// ==========================================================================
// PromptComposeResultContractTest.cpp
//
// WP04-T004-B: Contract test for PromptComposeResult.h and
// PromptComposeResultGuards.h.
//
// Validates:
//   - Required PromptComposeResult fields are present and meaningful.
//   - PromptComposeResult stays within ADR-006 §6.3 result-only semantics.
//   - PromptBoundaryContracts rejects memory/context write-back field names
//     that must never be introduced into PromptComposeResult.
//
// Verification command (WP04-T004):
//   cmake --build build-ci --target dasall_contract_tests
//   ctest --test-dir build-ci -R PromptComposeResultContractTest --output-on-failure
// ==========================================================================

#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "dasall/tests/support/TestAssertions.h"
#include "prompt/PromptBoundaryContracts.h"
#include "prompt/PromptComposeResult.h"
#include "prompt/PromptComposeResultGuards.h"

namespace {

using dasall::contracts::PromptBoundaryDecision;
using dasall::contracts::PromptComposeResult;
using dasall::contracts::evaluate_compose_result_field_boundary;
using dasall::contracts::validate_prompt_compose_result_boundary;
using dasall::contracts::validate_prompt_compose_result_required_fields;

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

void test_valid_minimal_result_passes_required_fields() {
  const auto result = make_valid_result();
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(guard.ok,
              "minimal valid PromptComposeResult must pass required-field guard");
}

void test_valid_minimal_result_passes_boundary() {
  const auto result = make_valid_result();
  const auto guard = validate_prompt_compose_result_boundary(result);
  assert_true(guard.ok,
              "minimal valid PromptComposeResult must pass boundary guard");
}

void test_allowed_result_field_names_pass_boundary_catalog() {
  assert_true(evaluate_compose_result_field_boundary("messages").allowed,
              "messages must be allowed in PromptComposeResult");
  assert_true(evaluate_compose_result_field_boundary("selected_prompt_id").allowed,
              "selected_prompt_id must be allowed in PromptComposeResult");
  assert_true(evaluate_compose_result_field_boundary("estimated_tokens").allowed,
              "estimated_tokens must be allowed in PromptComposeResult");
}

void test_memory_writeback_fields_rejected_by_boundary_catalog() {
  const auto memory_write_back = evaluate_compose_result_field_boundary("memory_write_back");
  assert_true(!memory_write_back.allowed,
              "memory_write_back must be rejected in PromptComposeResult");
  assert_equal(static_cast<int>(PromptBoundaryDecision::RejectComposeResultMemoryWriteback),
               static_cast<int>(memory_write_back.decision),
               "memory_write_back must map to compose-result rejection");

  const auto belief_patch = evaluate_compose_result_field_boundary("belief_patch");
  assert_true(!belief_patch.allowed,
              "belief_patch must be rejected in PromptComposeResult");

  const auto context_update = evaluate_compose_result_field_boundary("context_update");
  assert_true(!context_update.allowed,
              "context_update must be rejected in PromptComposeResult");
}

void test_missing_messages_rejected() {
  auto result = make_valid_result();
  result.messages = std::nullopt;
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "missing messages must be rejected");
  assert_equal("messages are required and must contain at least one item",
               std::string(guard.reason),
               "missing messages must return canonical reason");
}

void test_empty_messages_vector_rejected() {
  auto result = make_valid_result();
  result.messages = std::vector<std::string>{};
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "empty messages vector must be rejected");
}

void test_empty_message_element_rejected() {
  auto result = make_valid_result();
  result.messages = std::vector<std::string>{"system:plan", ""};
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "empty message element must be rejected");
  assert_equal("messages must not contain empty-string elements",
               std::string(guard.reason),
               "empty message element must return canonical reason");
}

void test_missing_selected_prompt_id_rejected() {
  auto result = make_valid_result();
  result.selected_prompt_id = std::nullopt;
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "missing selected_prompt_id must be rejected");
}

void test_missing_selected_version_rejected() {
  auto result = make_valid_result();
  result.selected_version = std::nullopt;
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "missing selected_version must be rejected");
}

void test_missing_estimated_tokens_rejected() {
  auto result = make_valid_result();
  result.estimated_tokens = std::nullopt;
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "missing estimated_tokens must be rejected");
}

void test_negative_estimated_tokens_rejected() {
  auto result = make_valid_result();
  result.estimated_tokens = -1;
  const auto guard = validate_prompt_compose_result_required_fields(result);
  assert_true(!guard.ok, "negative estimated_tokens must be rejected");
  assert_equal("estimated_tokens is required and must be non-negative",
               std::string(guard.reason),
               "negative estimated_tokens must return canonical reason");
}

}  // namespace

int main() {
  try {
    test_valid_minimal_result_passes_required_fields();
    test_valid_minimal_result_passes_boundary();
    test_allowed_result_field_names_pass_boundary_catalog();
    test_memory_writeback_fields_rejected_by_boundary_catalog();

    test_missing_messages_rejected();
    test_empty_messages_vector_rejected();
    test_empty_message_element_rejected();
    test_missing_selected_prompt_id_rejected();
    test_missing_selected_version_rejected();
    test_missing_estimated_tokens_rejected();
    test_negative_estimated_tokens_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
