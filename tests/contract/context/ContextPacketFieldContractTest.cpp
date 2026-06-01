// WP03-T011-B: ContextPacket field-level contract tests.
//
// Validates the WP03-T011 ContextPacketGuards three-layer stacking:
//   Layer 1 (validate_context_packet_required_fields):
//     4 required fields must be present with meaningful values.
//   Layer 2 (validate_context_packet_boundary):
//     Inherits L1 + created_at positive-timestamp boundary.
//   Layer 3 (validate_context_packet_field_rules):
//     Inherits L2 + optional string non-empty + vector element non-empty
//     + tags legality.
//
// Test coverage:
//   Positive: 4 scenarios (minimal, full optional, first-turn, partial).
//   Negative: 14 scenarios covering required-field absence, empty-string
//             violations, vector boundary checks, tags illegality, and
//             Layer 1 regression through Layer 3.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "context/ContextPacket.h"
#include "context/ContextPacketGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ContextPacket;
using dasall::contracts::RetrievalEvidenceRef;
using dasall::contracts::validate_context_packet_boundary;
using dasall::contracts::validate_context_packet_field_rules;
using dasall::contracts::validate_context_packet_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal ContextPacket with all required fields.
// ---------------------------------------------------------------------------
ContextPacket make_valid_packet() {
  ContextPacket pkt;
  pkt.request_id = "req-011";
  pkt.user_turn = "What is the current build status?";
  pkt.current_goal_summary = "Report CI build results.";
  pkt.recent_history = std::vector<std::string>{
      "user: Show me test coverage.",
      "agent: Coverage is 97%."};
  return pkt;
}

// ===========================================================================
// Positive cases (4)
// ===========================================================================

// P1: Minimal valid ContextPacket — required fields only, L3 passes.
void test_p1_minimal_valid() {
  auto pkt = make_valid_packet();
  auto r = validate_context_packet_field_rules(pkt);
  assert_true(r.ok, "P1: minimal valid packet must pass L3 field rules");
}

// P2: Full ContextPacket with all optional fields set — L3 passes.
void test_p2_full_optional() {
  auto pkt = make_valid_packet();
  pkt.summary_memory = "Previous session covered deployment pipeline.";
  pkt.retrieval_evidence =
      std::vector<std::string>{"doc:deploy-spec", "kb:rollback-procedure"};
    pkt.retrieval_evidence_refs = std::vector<RetrievalEvidenceRef>{
      RetrievalEvidenceRef{
        .evidence_ref = "evidence-deploy-001",
        .source_ref = "doc:deploy-spec",
        .source_kind = "file",
        .summary_text = "Deployment specification summary",
        .trust_level = "high",
        .freshness = "Fresh",
        .anchor_locator = "section:rollback",
      }};
  pkt.latest_observation_digest_summary =
      "Deployment #55 succeeded with zero errors.";
  pkt.active_tools = std::vector<std::string>{"deploy_tool", "log_tool"};
  pkt.policy_digest = "Max 5 tool calls per turn.";
  pkt.input_safety_signal = dasall::contracts::InputSafetySignal{
      .injection_detected = false,
      .pii_detected = true,
      .reason_codes = std::vector<std::string>{"pii_email_detected"},
    };
  pkt.token_budget_report = "goal:200 history:600 evidence:300 tools:100";
  pkt.belief_state_summary = "Confirmed: deployment #55 stable.";
  pkt.created_at = 1710000011000;
  pkt.tags = std::vector<std::string>{"context", "deploy"};

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(r.ok, "P2: full optional packet must pass L3 field rules");
}

// P3: First-turn ContextPacket — empty recent_history vector is valid.
void test_p3_first_turn() {
  ContextPacket pkt;
  pkt.request_id = "req-first-turn";
  pkt.user_turn = "Hello, start a new session.";
  pkt.current_goal_summary = "Establish user intent.";
  pkt.recent_history = std::vector<std::string>{};  // empty on first turn

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(r.ok, "P3: first-turn empty history must pass L3");
}

// P4: Partial optional fields — only summary_memory and created_at set.
void test_p4_partial_optional() {
  auto pkt = make_valid_packet();
  pkt.summary_memory = "Some long-term memory content.";
  pkt.created_at = 1710000005000;

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(r.ok, "P4: partial optional fields must pass L3");
}

// ===========================================================================
// Negative cases: required field violations (L1 through L3)
// ===========================================================================

// N1: Missing request_id — L3 rejects (inherited from L1).
void test_n1_missing_request_id() {
  auto pkt = make_valid_packet();
  pkt.request_id = std::nullopt;

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N1: missing request_id must fail");
}

// N2: Empty request_id string — L3 rejects.
void test_n2_empty_request_id() {
  auto pkt = make_valid_packet();
  pkt.request_id = "";

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N2: empty request_id must fail");
}

// N3: Missing user_turn — L3 rejects.
void test_n3_missing_user_turn() {
  auto pkt = make_valid_packet();
  pkt.user_turn = std::nullopt;

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N3: missing user_turn must fail");
}

// N4: Missing recent_history (nullopt) — L3 rejects.
void test_n4_missing_recent_history() {
  auto pkt = make_valid_packet();
  pkt.recent_history = std::nullopt;

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N4: nullopt recent_history must fail");
}

// ===========================================================================
// Negative cases: boundary violations (L2 through L3)
// ===========================================================================

// N5: created_at is zero — L3 rejects (boundary from L2).
void test_n5_created_at_zero() {
  auto pkt = make_valid_packet();
  pkt.created_at = 0;

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N5: zero created_at must fail boundary check");
}

// N6: created_at is negative — L3 rejects.
void test_n6_created_at_negative() {
  auto pkt = make_valid_packet();
  pkt.created_at = -100;

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N6: negative created_at must fail boundary check");
}

// ===========================================================================
// Negative cases: optional string non-empty violations (L3)
// ===========================================================================

// N7: Empty summary_memory — L3 rejects.
void test_n7_empty_summary_memory() {
  auto pkt = make_valid_packet();
  pkt.summary_memory = "";

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N7: empty summary_memory must fail L3");
}

// N8: Empty latest_observation_digest_summary — L3 rejects.
void test_n8_empty_observation_digest() {
  auto pkt = make_valid_packet();
  pkt.latest_observation_digest_summary = "";

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N8: empty observation_digest_summary must fail L3");
}

// N9: Empty policy_digest — L3 rejects.
void test_n9_empty_policy_digest() {
  auto pkt = make_valid_packet();
  pkt.policy_digest = "";

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N9: empty policy_digest must fail L3");
}

// N10: Empty token_budget_report — L3 rejects.
void test_n10_empty_token_budget_report() {
  auto pkt = make_valid_packet();
  pkt.token_budget_report = "";

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N10: empty token_budget_report must fail L3");
}

// N11: Empty belief_state_summary — L3 rejects.
void test_n11_empty_belief_state_summary() {
  auto pkt = make_valid_packet();
  pkt.belief_state_summary = "";

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N11: empty belief_state_summary must fail L3");
}

// ===========================================================================
// Negative cases: vector field violations (L3)
// ===========================================================================

// N12: retrieval_evidence is an empty vector — L3 rejects.
void test_n12_empty_retrieval_evidence_vector() {
  auto pkt = make_valid_packet();
  pkt.retrieval_evidence = std::vector<std::string>{};

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N12: empty retrieval_evidence vector must fail L3");
}

// N13: active_tools contains an empty string — L3 rejects.
void test_n13_active_tools_empty_entry() {
  auto pkt = make_valid_packet();
  pkt.active_tools = std::vector<std::string>{"valid_tool", ""};

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N13: active_tools with empty entry must fail L3");
}

// N14: retrieval_evidence_refs is an empty vector — L3 rejects.
void test_n14_empty_retrieval_evidence_refs_vector() {
  auto pkt = make_valid_packet();
  pkt.retrieval_evidence_refs = std::vector<RetrievalEvidenceRef>{};

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok,
              "N14: empty retrieval_evidence_refs vector must fail L3");
}

// N15: retrieval_evidence_refs contains an inconsistent ref — L3 rejects.
void test_n15_inconsistent_retrieval_evidence_ref() {
  auto pkt = make_valid_packet();
  pkt.retrieval_evidence_refs = std::vector<RetrievalEvidenceRef>{
      RetrievalEvidenceRef{
          .evidence_ref = "",
          .source_ref = "doc:deploy-spec",
          .source_kind = "file",
          .summary_text = "Deployment specification summary",
          .trust_level = "high",
          .freshness = "Fresh",
          .anchor_locator = std::nullopt,
      }};

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok,
              "N15: inconsistent retrieval_evidence_ref must fail L3");
}

// ===========================================================================
// Negative cases: tags violations (L3)
// ===========================================================================

// N16: tags is an empty vector — L3 rejects.
void test_n16_empty_tags_vector() {
  auto pkt = make_valid_packet();
  pkt.tags = std::vector<std::string>{};

  auto r = validate_context_packet_field_rules(pkt);
  assert_true(!r.ok, "N16: empty tags vector must fail L3");
}

// ===========================================================================
// Test runner
// ===========================================================================

int run_all_tests() {
  int passed = 0;
  int failed = 0;

  auto run = [&](void (*fn)(), const char* name) {
    try {
      fn();
      ++passed;
    } catch (const std::exception& e) {
      std::cerr << "FAIL [" << name << "]: " << e.what() << "\n";
      ++failed;
    }
  };

  // Positive cases (4).
  run(test_p1_minimal_valid, "P1_minimal_valid");
  run(test_p2_full_optional, "P2_full_optional");
  run(test_p3_first_turn, "P3_first_turn");
  run(test_p4_partial_optional, "P4_partial_optional");

  // Negative cases (16).
  run(test_n1_missing_request_id, "N1_missing_request_id");
  run(test_n2_empty_request_id, "N2_empty_request_id");
  run(test_n3_missing_user_turn, "N3_missing_user_turn");
  run(test_n4_missing_recent_history, "N4_missing_recent_history");
  run(test_n5_created_at_zero, "N5_created_at_zero");
  run(test_n6_created_at_negative, "N6_created_at_negative");
  run(test_n7_empty_summary_memory, "N7_empty_summary_memory");
  run(test_n8_empty_observation_digest, "N8_empty_obs_digest");
  run(test_n9_empty_policy_digest, "N9_empty_policy_digest");
  run(test_n10_empty_token_budget_report, "N10_empty_budget_report");
  run(test_n11_empty_belief_state_summary, "N11_empty_belief_summary");
  run(test_n12_empty_retrieval_evidence_vector, "N12_empty_evidence_vec");
  run(test_n13_active_tools_empty_entry, "N13_tools_empty_entry");
    run(test_n14_empty_retrieval_evidence_refs_vector,
      "N14_empty_evidence_refs_vec");
    run(test_n15_inconsistent_retrieval_evidence_ref,
      "N15_inconsistent_evidence_ref");
    run(test_n16_empty_tags_vector, "N16_empty_tags_vector");

  std::cout << "ContextPacketFieldContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}
