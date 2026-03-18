// WP03-T012-B: Checkpoint recovery-boundary contract tests.
//
// Validates the WP03-T012 Checkpoint minimal recovery semantics and
// boundary guard compliance:
//   - Checkpoint struct maps all 5 architecture §3.8.3 required items.
//   - CheckpointState enum covers all §6.10 interruption scenarios.
//   - Layer 1 (validate_checkpoint_required_fields): 5 required fields.
//   - Layer 2 (validate_checkpoint_boundary): inherits L1 + enum range
//     + optional field boundary constraints.
//
// Test coverage:
//   Positive: 4 scenarios (minimal, full optional, empty pending_action,
//             all enum states).
//   Negative: 14 scenarios covering required-field absence, Unspecified
//             state, empty-string violations, optional boundary checks,
//             and regression through Layer 2.

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
using dasall::contracts::validate_checkpoint_boundary;
using dasall::contracts::validate_checkpoint_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal Checkpoint with all required fields.
// Uses Running state with a non-empty pending_action.
// ---------------------------------------------------------------------------
Checkpoint make_valid_checkpoint() {
  Checkpoint cp;
  cp.checkpoint_id = "ckpt-012-001";
  cp.state = CheckpointState::Running;
  cp.step_id = "step-plan-execute";
  cp.working_memory_snapshot = "wm-ref:session-42:snapshot-7";
  cp.pending_action = "invoke tool: ci_query_tool";
  return cp;
}

// ===========================================================================
// Positive cases (4)
// ===========================================================================

// P1: Minimal valid Checkpoint — required fields only, L2 passes.
void test_p1_minimal_valid() {
  auto cp = make_valid_checkpoint();
  auto r = validate_checkpoint_boundary(cp);
  assert_true(r.ok, "P1: minimal valid checkpoint must pass L2 boundary");
}

// P2: Full Checkpoint with all optional fields set — L2 passes.
void test_p2_full_optional() {
  auto cp = make_valid_checkpoint();
  cp.request_id = "req-012";
  cp.goal_id = "goal-main";
  cp.belief_state_ref = "belief-session-42-v3";
  cp.retry_count = 2;
  cp.created_at = 1710000012000;
  cp.tags = std::vector<std::string>{"checkpoint", "recovery"};

  auto r = validate_checkpoint_boundary(cp);
  assert_true(r.ok, "P2: full optional checkpoint must pass L2 boundary");
}

// P3: Checkpoint with empty pending_action — valid ("no pending action").
void test_p3_empty_pending_action() {
  auto cp = make_valid_checkpoint();
  cp.pending_action = "";  // explicitly "no pending action"

  auto r = validate_checkpoint_boundary(cp);
  assert_true(r.ok,
              "P3: empty pending_action is valid (no action pending)");
}

// P4: All valid CheckpointState enum values pass L2.
void test_p4_all_valid_states() {
  const CheckpointState valid_states[] = {
      CheckpointState::Running,
      CheckpointState::Paused,
      CheckpointState::WaitingConfirm,
      CheckpointState::WaitingTool,
      CheckpointState::Failed,
      CheckpointState::Succeeded,
  };

  for (auto s : valid_states) {
    auto cp = make_valid_checkpoint();
    cp.state = s;
    auto r = validate_checkpoint_boundary(cp);
    assert_true(r.ok, "P4: all known non-Unspecified states must pass");
  }
}

// ===========================================================================
// Negative cases: required field violations (L1 through L2)
// ===========================================================================

// N1: Missing checkpoint_id — L2 rejects (inherited from L1).
void test_n1_missing_checkpoint_id() {
  auto cp = make_valid_checkpoint();
  cp.checkpoint_id = std::nullopt;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N1: missing checkpoint_id must fail");
}

// N2: Empty checkpoint_id string — L2 rejects.
void test_n2_empty_checkpoint_id() {
  auto cp = make_valid_checkpoint();
  cp.checkpoint_id = "";

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N2: empty checkpoint_id must fail");
}

// N3: Missing state (nullopt) — L2 rejects.
void test_n3_missing_state() {
  auto cp = make_valid_checkpoint();
  cp.state = std::nullopt;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N3: missing state must fail");
}

// N4: Unspecified state — L2 rejects.
void test_n4_unspecified_state() {
  auto cp = make_valid_checkpoint();
  cp.state = CheckpointState::Unspecified;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N4: Unspecified state must fail");
}

// N5: Missing step_id — L2 rejects.
void test_n5_missing_step_id() {
  auto cp = make_valid_checkpoint();
  cp.step_id = std::nullopt;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N5: missing step_id must fail");
}

// N6: Empty step_id string — L2 rejects.
void test_n6_empty_step_id() {
  auto cp = make_valid_checkpoint();
  cp.step_id = "";

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N6: empty step_id must fail");
}

// N7: Missing working_memory_snapshot — L2 rejects.
void test_n7_missing_wm_snapshot() {
  auto cp = make_valid_checkpoint();
  cp.working_memory_snapshot = std::nullopt;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N7: missing working_memory_snapshot must fail");
}

// N8: Empty working_memory_snapshot — L2 rejects.
void test_n8_empty_wm_snapshot() {
  auto cp = make_valid_checkpoint();
  cp.working_memory_snapshot = "";

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N8: empty working_memory_snapshot must fail");
}

// N9: Missing pending_action (nullopt) — L2 rejects.
void test_n9_missing_pending_action() {
  auto cp = make_valid_checkpoint();
  cp.pending_action = std::nullopt;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N9: nullopt pending_action must fail");
}

// ===========================================================================
// Negative cases: boundary violations (L2)
// ===========================================================================

// N10: request_id present but empty — L2 rejects.
void test_n10_empty_request_id() {
  auto cp = make_valid_checkpoint();
  cp.request_id = "";

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N10: empty request_id must fail boundary check");
}

// N11: goal_id present but empty — L2 rejects.
void test_n11_empty_goal_id() {
  auto cp = make_valid_checkpoint();
  cp.goal_id = "";

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N11: empty goal_id must fail boundary check");
}

// N12: belief_state_ref present but empty — L2 rejects.
void test_n12_empty_belief_state_ref() {
  auto cp = make_valid_checkpoint();
  cp.belief_state_ref = "";

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok,
              "N12: empty belief_state_ref must fail boundary check");
}

// N13: created_at is zero — L2 rejects.
void test_n13_created_at_zero() {
  auto cp = make_valid_checkpoint();
  cp.created_at = 0;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N13: zero created_at must fail boundary check");
}

// N14: created_at is negative — L2 rejects.
void test_n14_created_at_negative() {
  auto cp = make_valid_checkpoint();
  cp.created_at = -500;

  auto r = validate_checkpoint_boundary(cp);
  assert_true(!r.ok, "N14: negative created_at must fail boundary check");
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
  run(test_p3_empty_pending_action, "P3_empty_pending_action");
  run(test_p4_all_valid_states, "P4_all_valid_states");

  // Negative cases (14).
  run(test_n1_missing_checkpoint_id, "N1_missing_checkpoint_id");
  run(test_n2_empty_checkpoint_id, "N2_empty_checkpoint_id");
  run(test_n3_missing_state, "N3_missing_state");
  run(test_n4_unspecified_state, "N4_unspecified_state");
  run(test_n5_missing_step_id, "N5_missing_step_id");
  run(test_n6_empty_step_id, "N6_empty_step_id");
  run(test_n7_missing_wm_snapshot, "N7_missing_wm_snapshot");
  run(test_n8_empty_wm_snapshot, "N8_empty_wm_snapshot");
  run(test_n9_missing_pending_action, "N9_missing_pending_action");
  run(test_n10_empty_request_id, "N10_empty_request_id");
  run(test_n11_empty_goal_id, "N11_empty_goal_id");
  run(test_n12_empty_belief_state_ref, "N12_empty_belief_state_ref");
  run(test_n13_created_at_zero, "N13_created_at_zero");
  run(test_n14_created_at_negative, "N14_created_at_negative");

  std::cout << "CheckpointContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}
