#include <exception>
#include <iostream>
#include <string>

#include "boundary/ContextBoundaryGuards.h"
#include "support/TestAssertions.h"

namespace {

void test_allowed_context_field_passes_boundary_guard() {
  using dasall::contracts::ContextBoundaryDecision;
  using dasall::contracts::evaluate_context_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_context_field_boundary("recent_history");

  // Positive case: semantic context fields should remain admissible.
  assert_true(result.allowed,
              "recent_history should be allowed in ContextPacket");
  assert_equal(static_cast<int>(ContextBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed field should return allow decision");
}

void test_forbidden_final_messages_field_is_rejected() {
  using dasall::contracts::ContextBoundaryDecision;
  using dasall::contracts::evaluate_context_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_context_field_boundary("final_messages");

  // Negative case: final_messages belongs to prompt/message layer, not context.
  assert_true(!result.allowed,
              "final_messages must be rejected by ContextPacket boundary guard");
  assert_equal(static_cast<int>(ContextBoundaryDecision::RejectForbiddenField),
               static_cast<int>(result.decision),
               "forbidden field should return reject decision");
}

void test_forbidden_provider_payload_field_is_rejected() {
  using dasall::contracts::evaluate_context_field_boundary;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_context_field_boundary("provider_payload");

  // Negative case: provider payload is transport/provider-facing data.
  assert_true(!result.allowed,
              "provider_payload must be rejected by ContextPacket boundary guard");
}

void test_forbidden_rendered_prompt_field_is_rejected() {
  using dasall::contracts::evaluate_context_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_context_field_boundary("rendered_prompt");

  // Negative case: rendered prompt is composed output, not semantic context.
  assert_true(!result.allowed,
              "rendered_prompt must be rejected by ContextPacket boundary guard");
  assert_equal("context packet must not contain message or provider payload fields",
               std::string(result.reason),
               "forbidden fields should return a normalized rejection reason");
}

}  // namespace

int main() {
  try {
    test_allowed_context_field_passes_boundary_guard();
    test_forbidden_final_messages_field_is_rejected();
    test_forbidden_provider_payload_field_is_rejected();
    test_forbidden_rendered_prompt_field_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}