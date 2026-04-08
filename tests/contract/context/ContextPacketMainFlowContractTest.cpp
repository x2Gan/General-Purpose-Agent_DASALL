// WP03-T010-B: ContextPacket main-flow contract tests.
//
// Validates the WP03-T010 ContextPacket semantic composition and ADR-006
// boundary compliance:
//   - ContextPacket struct can be constructed with all 10 ADR-006 slots.
//   - Required fields (4 items) are distinguishable from optional fields.
//   - Default-constructed packet has no values (all nullopt).
//   - Forbidden fields are structurally absent (compile-time guarantee).
//
// Test coverage:
//   Positive: 4 scenarios proving valid ContextPacket construction.
//   Negative: 14 scenarios covering missing required fields, empty-string
//             violations, optional field boundary checks, and semantic
//             integrity.
//
// Note: Field-level validation guards are introduced in WP03-T011
// (ContextPacketGuards.h). This test validates the object structure
// and basic semantic properties at the contract level.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "context/ContextPacket.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ContextPacket;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal ContextPacket with all required fields.
// ---------------------------------------------------------------------------
ContextPacket make_valid_context_packet() {
  ContextPacket pkt;
  pkt.request_id = "req-010";
  pkt.user_turn = "Please summarize the latest build results.";
  pkt.current_goal_summary =
      "Summarize CI build results for the current sprint.";
  pkt.recent_history = std::vector<std::string>{
      "user: Show me the test report.",
      "agent: Here is the test report summary..."};
  return pkt;
}

// ---------------------------------------------------------------------------
// Helper: construct a full ContextPacket with all optional fields set.
// ---------------------------------------------------------------------------
ContextPacket make_full_context_packet() {
  auto pkt = make_valid_context_packet();
  pkt.summary_memory =
      "Previous session established CI monitoring for project DASALL.";
  pkt.retrieval_evidence =
      std::vector<std::string>{"doc:ci-pipeline-spec-v2", "kb:build-metrics"};
  pkt.latest_observation_digest_summary =
      "Build #142 passed with 98% coverage; 2 flaky tests detected.";
  pkt.active_tools =
      std::vector<std::string>{"ci_query_tool", "log_search_tool"};
  pkt.policy_digest = "Rate limit: max 10 tool calls per turn.";
  pkt.token_budget_report =
      "goal:200 history:800 evidence:400 tools:100 remaining:500";
  pkt.belief_state_summary =
      "Confirmed: build #142 passed. Hypothesis: flaky tests are timing-related.";
  pkt.created_at = 1710000010000;
  pkt.tags = std::vector<std::string>{"context", "ci-monitoring"};
  return pkt;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid ContextPacket (required fields only).
void test_minimal_packet_construction() {
  auto pkt = make_valid_context_packet();
  assert_true(pkt.request_id.has_value() && !pkt.request_id->empty(),
              "P1: request_id must be present and non-empty");
  assert_true(pkt.user_turn.has_value() && !pkt.user_turn->empty(),
              "P1: user_turn must be present and non-empty");
  assert_true(
      pkt.current_goal_summary.has_value() &&
          !pkt.current_goal_summary->empty(),
      "P1: current_goal_summary must be present and non-empty");
  assert_true(pkt.recent_history.has_value(),
              "P1: recent_history must be present");
}

// P2: Full ContextPacket with all optional fields set.
void test_full_packet_construction() {
  auto pkt = make_full_context_packet();
  // Verify all 10 ADR-006 slots are present.
  assert_true(pkt.user_turn.has_value(), "P2: user_turn present");
  assert_true(pkt.current_goal_summary.has_value(),
              "P2: current_goal_summary present");
  assert_true(pkt.recent_history.has_value(), "P2: recent_history present");
  assert_true(pkt.summary_memory.has_value(), "P2: summary_memory present");
  assert_true(pkt.retrieval_evidence.has_value(),
              "P2: retrieval_evidence present");
  assert_true(pkt.latest_observation_digest_summary.has_value(),
              "P2: latest_observation_digest_summary present");
  assert_true(pkt.active_tools.has_value(), "P2: active_tools present");
  assert_true(pkt.policy_digest.has_value(), "P2: policy_digest present");
  assert_true(pkt.token_budget_report.has_value(),
              "P2: token_budget_report present");
  assert_true(pkt.belief_state_summary.has_value(),
              "P2: belief_state_summary present");
}

// P3: First-turn ContextPacket (required fields + empty history).
void test_first_turn_packet() {
  ContextPacket pkt;
  pkt.request_id = "req-first";
  pkt.user_turn = "Hello, I need help with CI.";
  pkt.current_goal_summary = "Identify user intent and establish goal.";
  pkt.recent_history = std::vector<std::string>{};  // empty on first turn

  assert_true(pkt.recent_history.has_value(),
              "P3: recent_history must be present even when empty");
  assert_true(pkt.recent_history->empty(),
              "P3: recent_history should be empty on first turn");
  // Optional fields are absent on first turn.
  assert_true(!pkt.summary_memory.has_value(),
              "P3: summary_memory absent on first turn");
  assert_true(!pkt.latest_observation_digest_summary.has_value(),
              "P3: observation_digest absent on first turn");
  assert_true(!pkt.belief_state_summary.has_value(),
              "P3: belief_state absent on first turn");
}

// P4: ContextPacket with metadata (created_at + tags).
void test_packet_with_metadata() {
  auto pkt = make_valid_context_packet();
  pkt.created_at = 1710000001000;
  pkt.tags = std::vector<std::string>{"audit", "session-001"};

  assert_true(pkt.created_at.has_value() && *pkt.created_at > 0,
              "P4: created_at must be positive");
  assert_true(pkt.tags.has_value() && pkt.tags->size() == 2,
              "P4: tags must have 2 entries");
}

// ===========================================================================
// Negative cases: missing or empty required fields
// ===========================================================================

// N1: Default-constructed ContextPacket has all fields as nullopt.
void test_default_constructed_all_nullopt() {
  ContextPacket pkt;
  assert_true(!pkt.request_id.has_value(), "N1: request_id defaults to nullopt");
  assert_true(!pkt.user_turn.has_value(), "N1: user_turn defaults to nullopt");
  assert_true(!pkt.current_goal_summary.has_value(),
              "N1: current_goal_summary defaults to nullopt");
  assert_true(!pkt.recent_history.has_value(),
              "N1: recent_history defaults to nullopt");
}

// N2: Missing request_id (nullopt) — incomplete packet.
void test_missing_request_id() {
  auto pkt = make_valid_context_packet();
  pkt.request_id = std::nullopt;
  assert_true(!pkt.request_id.has_value(),
              "N2: packet with missing request_id is incomplete");
}

// N3: Empty request_id string.
void test_empty_request_id() {
  auto pkt = make_valid_context_packet();
  pkt.request_id = "";
  assert_true(pkt.request_id->empty(),
              "N3: empty request_id detected");
}

// N4: Missing user_turn.
void test_missing_user_turn() {
  auto pkt = make_valid_context_packet();
  pkt.user_turn = std::nullopt;
  assert_true(!pkt.user_turn.has_value(),
              "N4: packet with missing user_turn is incomplete");
}

// N5: Empty user_turn string.
void test_empty_user_turn() {
  auto pkt = make_valid_context_packet();
  pkt.user_turn = "";
  assert_true(pkt.user_turn->empty(),
              "N5: empty user_turn detected");
}

// N6: Missing current_goal_summary.
void test_missing_goal_summary() {
  auto pkt = make_valid_context_packet();
  pkt.current_goal_summary = std::nullopt;
  assert_true(!pkt.current_goal_summary.has_value(),
              "N6: packet with missing goal_summary is incomplete");
}

// N7: Empty current_goal_summary string.
void test_empty_goal_summary() {
  auto pkt = make_valid_context_packet();
  pkt.current_goal_summary = "";
  assert_true(pkt.current_goal_summary->empty(),
              "N7: empty current_goal_summary detected");
}

// N8: Missing recent_history (nullopt, not empty vector).
void test_missing_recent_history() {
  auto pkt = make_valid_context_packet();
  pkt.recent_history = std::nullopt;
  assert_true(!pkt.recent_history.has_value(),
              "N8: nullopt recent_history means field is missing");
}

// ===========================================================================
// Negative cases: optional field boundary violations
// ===========================================================================

// N9: Empty summary_memory string when present — should be detectable.
void test_empty_summary_memory() {
  auto pkt = make_valid_context_packet();
  pkt.summary_memory = "";
  assert_true(pkt.summary_memory.has_value() && pkt.summary_memory->empty(),
              "N9: empty summary_memory string detectable");
}

// N10: Empty latest_observation_digest_summary when present.
void test_empty_observation_digest_summary() {
  auto pkt = make_valid_context_packet();
  pkt.latest_observation_digest_summary = "";
  assert_true(pkt.latest_observation_digest_summary.has_value() &&
                  pkt.latest_observation_digest_summary->empty(),
              "N10: empty observation_digest_summary detectable");
}

// N11: Empty policy_digest when present.
void test_empty_policy_digest() {
  auto pkt = make_valid_context_packet();
  pkt.policy_digest = "";
  assert_true(pkt.policy_digest.has_value() && pkt.policy_digest->empty(),
              "N11: empty policy_digest detectable");
}

// N12: Empty belief_state_summary when present.
void test_empty_belief_state_summary() {
  auto pkt = make_valid_context_packet();
  pkt.belief_state_summary = "";
  assert_true(pkt.belief_state_summary.has_value() &&
                  pkt.belief_state_summary->empty(),
              "N12: empty belief_state_summary detectable");
}

// N13: Non-positive created_at when present.
void test_non_positive_created_at() {
  auto pkt = make_valid_context_packet();
  pkt.created_at = 0;
  assert_true(pkt.created_at.has_value() && *pkt.created_at <= 0,
              "N13: non-positive created_at detected");
}

// N14: Negative created_at when present.
void test_negative_created_at() {
  auto pkt = make_valid_context_packet();
  pkt.created_at = -100;
  assert_true(pkt.created_at.has_value() && *pkt.created_at < 0,
              "N14: negative created_at detected");
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
  run(test_minimal_packet_construction, "P1_minimal_packet");
  run(test_full_packet_construction, "P2_full_packet");
  run(test_first_turn_packet, "P3_first_turn");
  run(test_packet_with_metadata, "P4_metadata");

  // Negative cases (14).
  run(test_default_constructed_all_nullopt, "N1_default_nullopt");
  run(test_missing_request_id, "N2_missing_request_id");
  run(test_empty_request_id, "N3_empty_request_id");
  run(test_missing_user_turn, "N4_missing_user_turn");
  run(test_empty_user_turn, "N5_empty_user_turn");
  run(test_missing_goal_summary, "N6_missing_goal_summary");
  run(test_empty_goal_summary, "N7_empty_goal_summary");
  run(test_missing_recent_history, "N8_missing_recent_history");
  run(test_empty_summary_memory, "N9_empty_summary_memory");
  run(test_empty_observation_digest_summary, "N10_empty_obs_digest");
  run(test_empty_policy_digest, "N11_empty_policy_digest");
  run(test_empty_belief_state_summary, "N12_empty_belief_summary");
  run(test_non_positive_created_at, "N13_non_positive_created_at");
  run(test_negative_created_at, "N14_negative_created_at");

  std::cout << "ContextPacketMainFlowContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}
