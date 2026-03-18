// ==========================================================================
// PromptBoundaryContractsSmokeTest.cpp
//
// WP04-T001-B: Smoke test for PromptBoundaryContracts.h.
//
// Validates that the three ADR-006-derived Prompt boundary guards behave
// correctly for both legal and illegal field names:
//
//   ContextPacket guard (evaluate_context_packet_prompt_field_boundary):
//     Positive (P1): semantic context field "user_turn" is allowed.
//     Negative (N1): message-rendering field "final_messages" is rejected.
//
//   PromptComposeRequest guard (evaluate_compose_request_field_boundary):
//     Positive (P2): assembly-request field "stage" is allowed.
//     Negative (N2): context-ownership field "memory_snapshot" is rejected.
//
//   PromptComposeResult guard (evaluate_compose_result_field_boundary):
//     Positive (P3): assembly-output field "messages" is allowed.
//     Negative (N3): memory-writeback field "memory_write_back" is rejected.
//
//   Cross-boundary regression matrix (P4 / N4):
//     Positive (P4): Boolean helpers return true for all canonical legal names.
//     Negative (N4): Each forbidden-field table's first entry is rejected
//                    with the correct decision code.
//
// Verification command (WP04-T001):
//   cmake --build build-ci --target dasall_contract_tests && \
//   ctest --test-dir build-ci -R PromptBoundaryContractsSmokeTest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>

#include "prompt/PromptBoundaryContracts.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::PromptBoundaryDecision;
using dasall::contracts::PromptBoundaryResult;
using dasall::contracts::evaluate_context_packet_prompt_field_boundary;
using dasall::contracts::evaluate_compose_request_field_boundary;
using dasall::contracts::evaluate_compose_result_field_boundary;
using dasall::contracts::is_allowed_context_packet_prompt_field;
using dasall::contracts::is_allowed_compose_request_field;
using dasall::contracts::is_allowed_compose_result_field;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// =========================================================================
// P1 — ContextPacket: semantic context field is allowed
// =========================================================================
void test_context_packet_semantic_field_is_allowed() {
  // "user_turn" is one of the ADR-006 §6.1 required semantic slots.
  const PromptBoundaryResult result =
      evaluate_context_packet_prompt_field_boundary("user_turn");

  // Positive case: semantic slot fields must not be rejected.
  assert_true(result.allowed,
              "user_turn should be allowed in ContextPacket (ADR-006 §6.1 semantic slot)");
  assert_equal(static_cast<int>(PromptBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed field should return AllowField decision");
}

// =========================================================================
// N1 — ContextPacket: message-rendering field is rejected
// =========================================================================
void test_context_packet_message_field_is_rejected() {
  // "final_messages" is explicitly listed in ADR-006 §6.1 禁令.
  const PromptBoundaryResult result =
      evaluate_context_packet_prompt_field_boundary("final_messages");

  // Negative case: message-rendering fields must be blocked.
  assert_true(!result.allowed,
              "final_messages must be rejected in ContextPacket (ADR-006 §6.1)");
  assert_equal(static_cast<int>(PromptBoundaryDecision::RejectContextPacketMessageField),
               static_cast<int>(result.decision),
               "message-rendering field should return RejectContextPacketMessageField");
  assert_equal("context packet must not contain message-rendering-layer fields (ADR-006 §6.1)",
               std::string(result.reason),
               "rejection reason must match the canonical ADR-006 rationale");
}

// =========================================================================
// P2 — PromptComposeRequest: assembly-request field is allowed
// =========================================================================
void test_compose_request_stage_field_is_allowed() {
  // "stage" is a canonical PromptComposeRequest field per ADR-006 §6.2.
  const PromptBoundaryResult result =
      evaluate_compose_request_field_boundary("stage");

  // Positive case: assembly request fields must not be rejected.
  assert_true(result.allowed,
              "stage should be allowed in PromptComposeRequest (ADR-006 §6.2)");
  assert_equal(static_cast<int>(PromptBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed request field should return AllowField decision");
}

// =========================================================================
// N2 — PromptComposeRequest: context-ownership field is rejected
// =========================================================================
void test_compose_request_memory_snapshot_is_rejected() {
  // "memory_snapshot" would make PromptComposeRequest a second context owner,
  // violating ADR-006 §6.2 / §7 (option B rejected).
  const PromptBoundaryResult result =
      evaluate_compose_request_field_boundary("memory_snapshot");

  // Negative case: context-ownership fields must be blocked.
  assert_true(!result.allowed,
              "memory_snapshot must be rejected in PromptComposeRequest (ADR-006 §6.2)");
  assert_equal(static_cast<int>(PromptBoundaryDecision::RejectComposeRequestContextOwnership),
               static_cast<int>(result.decision),
               "context-ownership field should return RejectComposeRequestContextOwnership");
  assert_equal("compose request must not own context assembly data (ADR-006 §6.2)",
               std::string(result.reason),
               "rejection reason must match the canonical ADR-006 rationale");
}

// =========================================================================
// P3 — PromptComposeResult: assembly-output field is allowed
// =========================================================================
void test_compose_result_messages_field_is_allowed() {
  // "messages" is the primary output field of PromptComposeResult per
  // ADR-006 §6.3.
  const PromptBoundaryResult result =
      evaluate_compose_result_field_boundary("messages");

  // Positive case: assembly output fields must not be rejected.
  assert_true(result.allowed,
              "messages should be allowed in PromptComposeResult (ADR-006 §6.3)");
  assert_equal(static_cast<int>(PromptBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed result field should return AllowField decision");
}

// =========================================================================
// N3 — PromptComposeResult: memory-writeback field is rejected
// =========================================================================
void test_compose_result_memory_write_back_is_rejected() {
  // "memory_write_back" would give PromptComposer an illegal write path
  // into the memory subsystem (ADR-006 §3.3 条款 5 / §6.3).
  const PromptBoundaryResult result =
      evaluate_compose_result_field_boundary("memory_write_back");

  // Negative case: memory write-back fields must be blocked.
  assert_true(!result.allowed,
              "memory_write_back must be rejected in PromptComposeResult (ADR-006 §3.3)");
  assert_equal(static_cast<int>(PromptBoundaryDecision::RejectComposeResultMemoryWriteback),
               static_cast<int>(result.decision),
               "memory-writeback field should return RejectComposeResultMemoryWriteback");
  assert_equal("compose result must not contain memory write-back fields (ADR-006 §3.3)",
               std::string(result.reason),
               "rejection reason must match the canonical ADR-006 rationale");
}

// =========================================================================
// P4 — Boolean helpers: all canonical legal field names return true
// =========================================================================
void test_boolean_helpers_allow_canonical_legal_fields() {
  // Verify that the three boolean shortcut helpers each accept representative
  // legal field names without exposing a false positive.

  assert_true(is_allowed_context_packet_prompt_field("current_goal_summary"),
              "current_goal_summary should be allowed in ContextPacket");
  assert_true(is_allowed_context_packet_prompt_field("recent_history"),
              "recent_history should be allowed in ContextPacket");
  assert_true(is_allowed_compose_request_field("context_packet"),
              "context_packet should be allowed in PromptComposeRequest");
  assert_true(is_allowed_compose_request_field("model_route"),
              "model_route should be allowed in PromptComposeRequest");
  assert_true(is_allowed_compose_result_field("selected_prompt_id"),
              "selected_prompt_id should be allowed in PromptComposeResult");
  assert_true(is_allowed_compose_result_field("estimated_tokens"),
              "estimated_tokens should be allowed in PromptComposeResult");
}

// =========================================================================
// N4 — Regression matrix: first entry of each forbidden table is rejected
//      with the correct decision code
// =========================================================================
void test_regression_matrix_first_forbidden_entry_each_table() {
  // ContextPacket table: first entry is "final_messages".
  {
    const auto result = evaluate_context_packet_prompt_field_boundary("final_messages");
    assert_true(!result.allowed,
                "ContextPacket regression: final_messages must be rejected");
    assert_equal(static_cast<int>(PromptBoundaryDecision::RejectContextPacketMessageField),
                 static_cast<int>(result.decision),
                 "ContextPacket regression: decision must be RejectContextPacketMessageField");
  }

  // PromptComposeRequest table: first entry is "memory_snapshot".
  {
    const auto result = evaluate_compose_request_field_boundary("memory_snapshot");
    assert_true(!result.allowed,
                "ComposeRequest regression: memory_snapshot must be rejected");
    assert_equal(static_cast<int>(PromptBoundaryDecision::RejectComposeRequestContextOwnership),
                 static_cast<int>(result.decision),
                 "ComposeRequest regression: decision must be RejectComposeRequestContextOwnership");
  }

  // PromptComposeResult table: first entry is "memory_write_back".
  {
    const auto result = evaluate_compose_result_field_boundary("memory_write_back");
    assert_true(!result.allowed,
                "ComposeResult regression: memory_write_back must be rejected");
    assert_equal(static_cast<int>(PromptBoundaryDecision::RejectComposeResultMemoryWriteback),
                 static_cast<int>(result.decision),
                 "ComposeResult regression: decision must be RejectComposeResultMemoryWriteback");
  }

  // Additional negative cases — validate remaining important forbidden fields.
  assert_true(!is_allowed_context_packet_prompt_field("rendered_prompt"),
              "rendered_prompt must be blocked in ContextPacket");
  assert_true(!is_allowed_context_packet_prompt_field("provider_payload"),
              "provider_payload must be blocked in ContextPacket");
  assert_true(!is_allowed_compose_request_field("retrieval_candidates"),
              "retrieval_candidates must be blocked in PromptComposeRequest");
  assert_true(!is_allowed_compose_result_field("belief_patch"),
              "belief_patch must be blocked in PromptComposeResult");
  assert_true(!is_allowed_compose_result_field("context_update"),
              "context_update must be blocked in PromptComposeResult");
}

}  // namespace

int main() {
  try {
    // Positive cases
    test_context_packet_semantic_field_is_allowed();
    test_compose_request_stage_field_is_allowed();
    test_compose_result_messages_field_is_allowed();
    test_boolean_helpers_allow_canonical_legal_fields();

    // Negative cases
    test_context_packet_message_field_is_rejected();
    test_compose_request_memory_snapshot_is_rejected();
    test_compose_result_memory_write_back_is_rejected();
    test_regression_matrix_first_forbidden_entry_each_table();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
