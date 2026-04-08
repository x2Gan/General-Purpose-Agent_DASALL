// WP03-T009-B: BeliefState contract tests.
//
// Validates the WP03-T009 BeliefState main-flow positioning and boundary
// guards enforced by:
//   - validate_belief_state_required_fields()  (Layer 1)
//   - validate_belief_state_boundary()          (Layer 2)
//
// Test coverage:
//   Positive: 4 scenarios proving valid BeliefState states.
//   Negative: 14 scenarios covering missing required fields, confidence
//             range violations, optional field boundary violations, and
//             structural integrity checks.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/BeliefState.h"
#include "agent/BeliefStateGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::BeliefState;
using dasall::contracts::BeliefStateGuardResult;
using dasall::contracts::validate_belief_state_boundary;
using dasall::contracts::validate_belief_state_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal BeliefState with all required fields.
// ---------------------------------------------------------------------------
BeliefState make_valid_belief_state() {
  BeliefState state;
  state.request_id = "req-001";
  state.confirmed_facts =
      std::vector<std::string>{"file /tmp/out.txt exists", "size is 42 bytes"};
  state.hypotheses =
      std::vector<std::string>{"output format matches expected schema"};
  state.assumptions =
      std::vector<std::string>{"filesystem is writable"};
  state.evidence_refs =
      std::vector<std::string>{"obs-001", "digest-001"};
  state.confidence = 0.85f;
  return state;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid belief state (required fields only) passes Layer 1.
void test_minimal_state_passes_required_fields() {
  auto state = make_valid_belief_state();
  auto result = validate_belief_state_required_fields(state);
  assert_true(result.ok,
              "minimal valid belief state should pass required fields guard");
}

// P2: Minimal valid belief state passes Layer 2 boundary.
void test_minimal_state_passes_boundary() {
  auto state = make_valid_belief_state();
  auto result = validate_belief_state_boundary(state);
  assert_true(result.ok,
              "minimal valid belief state should pass boundary guard");
}

// P3: Full belief state with all optional fields set.
void test_full_state_passes_boundary() {
  auto state = make_valid_belief_state();
  state.goal_id = "goal-001";
  state.created_at = 1710000001000;
  state.tags = std::vector<std::string>{"cognition", "belief-update"};

  auto result = validate_belief_state_boundary(state);
  assert_true(result.ok,
              "full belief state with all optional fields should pass boundary");
}

// P4: Belief state with empty vectors (valid initial state).
void test_empty_vectors_pass() {
  auto state = make_valid_belief_state();
  state.confirmed_facts = std::vector<std::string>{};
  state.hypotheses = std::vector<std::string>{};
  state.assumptions = std::vector<std::string>{};
  state.evidence_refs = std::vector<std::string>{};
  state.confidence = 0.1f;  // low confidence reflects empty knowledge

  auto result = validate_belief_state_boundary(state);
  assert_true(result.ok,
              "belief state with empty vectors should pass (valid initial state)");
}

// ===========================================================================
// Negative cases: missing required fields
// ===========================================================================

// N1: Missing request_id.
void test_missing_request_id_fails() {
  auto state = make_valid_belief_state();
  state.request_id = std::nullopt;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "missing request_id should fail");
}

// N2: Empty request_id.
void test_empty_request_id_fails() {
  auto state = make_valid_belief_state();
  state.request_id = "";
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "empty request_id should fail");
}

// N3: Missing confirmed_facts.
void test_missing_confirmed_facts_fails() {
  auto state = make_valid_belief_state();
  state.confirmed_facts = std::nullopt;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "missing confirmed_facts should fail");
}

// N4: Missing hypotheses.
void test_missing_hypotheses_fails() {
  auto state = make_valid_belief_state();
  state.hypotheses = std::nullopt;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "missing hypotheses should fail");
}

// N5: Missing assumptions.
void test_missing_assumptions_fails() {
  auto state = make_valid_belief_state();
  state.assumptions = std::nullopt;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "missing assumptions should fail");
}

// N6: Missing evidence_refs.
void test_missing_evidence_refs_fails() {
  auto state = make_valid_belief_state();
  state.evidence_refs = std::nullopt;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "missing evidence_refs should fail");
}

// N7: Missing confidence.
void test_missing_confidence_fails() {
  auto state = make_valid_belief_state();
  state.confidence = std::nullopt;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "missing confidence should fail");
}

// ===========================================================================
// Negative cases: confidence range violations
// ===========================================================================

// N8: Confidence below 0.0.
void test_confidence_below_zero_fails() {
  auto state = make_valid_belief_state();
  state.confidence = -0.1f;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "confidence below 0.0 should fail");
}

// N9: Confidence above 1.0.
void test_confidence_above_one_fails() {
  auto state = make_valid_belief_state();
  state.confidence = 1.1f;
  auto result = validate_belief_state_required_fields(state);
  assert_true(!result.ok, "confidence above 1.0 should fail");
}

// N10: Confidence exactly 0.0 should pass (boundary).
void test_confidence_zero_passes() {
  auto state = make_valid_belief_state();
  state.confidence = 0.0f;
  auto result = validate_belief_state_required_fields(state);
  assert_true(result.ok, "confidence exactly 0.0 should pass");
}

// N11: Confidence exactly 1.0 should pass (boundary).
void test_confidence_one_passes() {
  auto state = make_valid_belief_state();
  state.confidence = 1.0f;
  auto result = validate_belief_state_required_fields(state);
  assert_true(result.ok, "confidence exactly 1.0 should pass");
}

// ===========================================================================
// Negative cases: optional field boundary violations
// ===========================================================================

// N12: goal_id is empty string when present.
void test_empty_goal_id_fails() {
  auto state = make_valid_belief_state();
  state.goal_id = "";
  auto result = validate_belief_state_boundary(state);
  assert_true(!result.ok, "empty goal_id should fail boundary");
}

// N13: created_at = 0 when present.
void test_zero_created_at_fails() {
  auto state = make_valid_belief_state();
  state.created_at = 0;
  auto result = validate_belief_state_boundary(state);
  assert_true(!result.ok, "zero created_at should fail boundary");
}

// N14: created_at negative when present.
void test_negative_created_at_fails() {
  auto state = make_valid_belief_state();
  state.created_at = -1000;
  auto result = validate_belief_state_boundary(state);
  assert_true(!result.ok, "negative created_at should fail boundary");
}

}  // namespace

int main() {
  try {
    // Positive cases (4)
    test_minimal_state_passes_required_fields();
    test_minimal_state_passes_boundary();
    test_full_state_passes_boundary();
    test_empty_vectors_pass();

    // Negative cases: missing required fields (7)
    test_missing_request_id_fails();
    test_empty_request_id_fails();
    test_missing_confirmed_facts_fails();
    test_missing_hypotheses_fails();
    test_missing_assumptions_fails();
    test_missing_evidence_refs_fails();
    test_missing_confidence_fails();

    // Negative cases: confidence range (4, including 2 boundary passes)
    test_confidence_below_zero_fails();
    test_confidence_above_one_fails();
    test_confidence_zero_passes();
    test_confidence_one_passes();

    // Negative cases: optional field boundary violations (3)
    test_empty_goal_id_fails();
    test_zero_created_at_fails();
    test_negative_created_at_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
