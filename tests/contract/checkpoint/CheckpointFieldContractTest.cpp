// WP03-T013-B: Checkpoint field-level contract tests.
//
// Validates the WP03-T013 Checkpoint field completeness rules (Layer 3)
// on top of T012 Layer 1 (required) + Layer 2 (boundary):
//   - tags: non-empty vector with no empty strings (unified pattern).
//   - state→pending_action consistency: waiting states (Paused,
//     WaitingConfirm, WaitingTool) require non-empty pending_action
//     (architecture §6.10).
//
// Test coverage:
//   Positive: 4 scenarios (minimal, full with tags, waiting states with
//             non-empty pending_action, non-waiting states with empty
//             pending_action).
//   Negative: 10 scenarios covering tags violations, state→pending_action
//             consistency violations, and L2 regression through L3.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::Checkpoint;
using dasall::contracts::CheckpointGuardResult;
using dasall::contracts::CheckpointState;
using dasall::contracts::validate_checkpoint_field_rules;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal Checkpoint with all required fields.
// Uses Running state with a non-empty pending_action.
// ---------------------------------------------------------------------------
Checkpoint make_valid_checkpoint() {
  Checkpoint cp;
  cp.checkpoint_id = "ckpt-013-001";
  cp.state = CheckpointState::Running;
  cp.step_id = "step-plan-execute";
  cp.working_memory_snapshot = "wm-ref:session-42:snapshot-7";
  cp.pending_action = "invoke tool: ci_query_tool";
  return cp;
}

// ===========================================================================
// Positive cases (4)
// ===========================================================================

// P1: Minimal valid Checkpoint — required fields only, L3 passes.
void test_p1_minimal_valid() {
  auto cp = make_valid_checkpoint();
  auto r = validate_checkpoint_field_rules(cp);
  assert_true(r.ok, "P1: minimal valid checkpoint must pass L3 field rules");
}

// P2: Full Checkpoint with valid tags — L3 passes.
void test_p2_full_with_tags() {
  auto cp = make_valid_checkpoint();
  cp.request_id = "req-013";
  cp.goal_id = "goal-main";
  cp.belief_state_ref = "belief-session-42-v3";
  cp.retry_count = 1;
  cp.created_at = 1710000013000;
  cp.tags = std::vector<std::string>{"checkpoint", "recovery", "audit"};

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(r.ok, "P2: full checkpoint with valid tags must pass L3");
}

// P3: Waiting states (Paused/WaitingConfirm/WaitingTool) with non-empty
//     pending_action — L3 passes (state→pending_action consistency).
void test_p3_waiting_states_with_pending_action() {
  const CheckpointState waiting_states[] = {
      CheckpointState::Paused,
      CheckpointState::WaitingConfirm,
      CheckpointState::WaitingTool,
  };

  for (auto s : waiting_states) {
    auto cp = make_valid_checkpoint();
    cp.state = s;
    cp.pending_action = "awaiting: user clarification on deployment scope";
    auto r = validate_checkpoint_field_rules(cp);
    assert_true(r.ok,
                "P3: waiting state with non-empty pending_action must pass");
  }
}

// P4: Non-waiting states (Running/Failed/Succeeded) with empty
//     pending_action — L3 passes (no constraint on these states).
void test_p4_non_waiting_states_empty_pending_action() {
  const CheckpointState non_waiting_states[] = {
      CheckpointState::Running,
      CheckpointState::Failed,
      CheckpointState::Succeeded,
  };

  for (auto s : non_waiting_states) {
    auto cp = make_valid_checkpoint();
    cp.state = s;
    cp.pending_action = "";  // empty is valid for non-waiting states
    auto r = validate_checkpoint_field_rules(cp);
    assert_true(r.ok,
                "P4: non-waiting state with empty pending_action must pass");
  }
}

// ===========================================================================
// Negative cases: tags violations (L3)
// ===========================================================================

// N1: tags present but empty vector — L3 rejects.
void test_n1_tags_empty_vector() {
  auto cp = make_valid_checkpoint();
  cp.tags = std::vector<std::string>{};

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N1: empty tags vector must fail L3");
}

// N2: tags contain an empty string — L3 rejects.
void test_n2_tags_contain_empty_string() {
  auto cp = make_valid_checkpoint();
  cp.tags = std::vector<std::string>{"checkpoint", "", "recovery"};

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N2: tags with empty string must fail L3");
}

// N3: tags with only empty strings — L3 rejects.
void test_n3_tags_all_empty() {
  auto cp = make_valid_checkpoint();
  cp.tags = std::vector<std::string>{"", ""};

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N3: tags with all empty strings must fail L3");
}

// ===========================================================================
// Negative cases: state→pending_action consistency violations (L3)
// ===========================================================================

// N4: Paused state with empty pending_action — L3 rejects.
void test_n4_paused_empty_pending_action() {
  auto cp = make_valid_checkpoint();
  cp.state = CheckpointState::Paused;
  cp.pending_action = "";

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok,
              "N4: Paused with empty pending_action must fail L3");
}

// N5: WaitingConfirm state with empty pending_action — L3 rejects.
void test_n5_waiting_confirm_empty_pending_action() {
  auto cp = make_valid_checkpoint();
  cp.state = CheckpointState::WaitingConfirm;
  cp.pending_action = "";

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok,
              "N5: WaitingConfirm with empty pending_action must fail L3");
}

// N6: WaitingTool state with empty pending_action — L3 rejects.
void test_n6_waiting_tool_empty_pending_action() {
  auto cp = make_valid_checkpoint();
  cp.state = CheckpointState::WaitingTool;
  cp.pending_action = "";

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok,
              "N6: WaitingTool with empty pending_action must fail L3");
}

// ===========================================================================
// Negative cases: L2 regression through L3
// ===========================================================================

// N7: Missing checkpoint_id — L3 rejects (inherited from L1/L2).
void test_n7_missing_checkpoint_id() {
  auto cp = make_valid_checkpoint();
  cp.checkpoint_id = std::nullopt;

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N7: missing checkpoint_id must fail L3 (via L1)");
}

// N8: Unspecified state — L3 rejects (inherited from L1/L2).
void test_n8_unspecified_state() {
  auto cp = make_valid_checkpoint();
  cp.state = CheckpointState::Unspecified;

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N8: Unspecified state must fail L3 (via L1)");
}

// N9: Empty request_id — L3 rejects (inherited from L2).
void test_n9_empty_request_id() {
  auto cp = make_valid_checkpoint();
  cp.request_id = "";

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N9: empty request_id must fail L3 (via L2)");
}

// N10: Negative created_at — L3 rejects (inherited from L2).
void test_n10_negative_created_at() {
  auto cp = make_valid_checkpoint();
  cp.created_at = -100;

  auto r = validate_checkpoint_field_rules(cp);
  assert_true(!r.ok, "N10: negative created_at must fail L3 (via L2)");
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
  run(test_p2_full_with_tags, "P2_full_with_tags");
  run(test_p3_waiting_states_with_pending_action,
      "P3_waiting_states_with_pending_action");
  run(test_p4_non_waiting_states_empty_pending_action,
      "P4_non_waiting_states_empty_pending_action");

  // Negative cases (10).
  run(test_n1_tags_empty_vector, "N1_tags_empty_vector");
  run(test_n2_tags_contain_empty_string, "N2_tags_contain_empty_string");
  run(test_n3_tags_all_empty, "N3_tags_all_empty");
  run(test_n4_paused_empty_pending_action,
      "N4_paused_empty_pending_action");
  run(test_n5_waiting_confirm_empty_pending_action,
      "N5_waiting_confirm_empty_pending_action");
  run(test_n6_waiting_tool_empty_pending_action,
      "N6_waiting_tool_empty_pending_action");
  run(test_n7_missing_checkpoint_id, "N7_missing_checkpoint_id");
  run(test_n8_unspecified_state, "N8_unspecified_state");
  run(test_n9_empty_request_id, "N9_empty_request_id");
  run(test_n10_negative_created_at, "N10_negative_created_at");

  std::cout << "CheckpointFieldContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}
