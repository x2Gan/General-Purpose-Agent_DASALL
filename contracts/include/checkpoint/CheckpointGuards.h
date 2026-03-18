#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "checkpoint/Checkpoint.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for Checkpoint validation, following the same pattern as
// BeliefStateGuardResult (WP03-T009) and GoalContractGuardResult (WP03-T004).
// ---------------------------------------------------------------------------
struct CheckpointGuardResult {
  bool ok = false;
  std::string_view reason = "checkpoint validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T012-B).
//
// Validates that all 5 required fields are present with meaningful values:
//   R1) checkpoint_id           — present and non-empty.
//   R2) state                   — present and not Unspecified.
//   R3) step_id                 — present and non-empty.
//   R4) working_memory_snapshot — present and non-empty.
//   R5) pending_action          — present (empty string allowed;
//                                  "no pending action" is a valid state).
//
// Design reference: WP03-T012-D §5.8 Layer 1.
// ---------------------------------------------------------------------------
inline CheckpointGuardResult validate_checkpoint_required_fields(
    const Checkpoint& cp) {
  // R1: checkpoint_id must be present and non-empty.
  if (!has_non_empty_value(cp.checkpoint_id)) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "checkpoint_id is required and must be non-empty",
    };
  }

  // R2: state must be present and not Unspecified.
  if (!cp.state.has_value() ||
      *cp.state == CheckpointState::Unspecified) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "state is required and must not be Unspecified",
    };
  }

  // R3: step_id must be present and non-empty.
  if (!has_non_empty_value(cp.step_id)) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "step_id is required and must be non-empty",
    };
  }

  // R4: working_memory_snapshot must be present and non-empty.
  // This is a reference/summary string — an empty snapshot reference
  // is not meaningful for recovery.
  if (!has_non_empty_value(cp.working_memory_snapshot)) {
    return CheckpointGuardResult{
        .ok = false,
        .reason =
            "working_memory_snapshot is required and must be non-empty",
    };
  }

  // R5: pending_action must be present (has_value).
  // Empty string is allowed — it means "no action is pending".
  // Nullopt means the field was never set, which is invalid.
  if (!cp.pending_action.has_value()) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "pending_action is required (use empty string for none)",
    };
  }

  return CheckpointGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T012-B).
//
// Validates semantic boundary rules on top of required fields:
//   1) All required field checks (Layer 1).
//   2) state must be within the known CheckpointState enum range.
//   3) request_id, if present, must be non-empty.
//   4) goal_id, if present, must be non-empty.
//   5) belief_state_ref, if present, must be non-empty.
//   6) created_at, if present, must be positive.
//
// Design reference: WP03-T012-D §5.8 Layer 2.
// ---------------------------------------------------------------------------
inline CheckpointGuardResult validate_checkpoint_boundary(
    const Checkpoint& cp) {
  // Layer 1: required field presence.
  auto required_result = validate_checkpoint_required_fields(cp);
  if (!required_result.ok) {
    return required_result;
  }

  // Boundary: CheckpointState enum range check.
  // Values must be between Running (1) and Succeeded (6) inclusive,
  // since Unspecified (0) is already rejected by Layer 1.
  const int raw_state = static_cast<int>(*cp.state);
  if (raw_state < static_cast<int>(CheckpointState::Unspecified) ||
      raw_state > static_cast<int>(CheckpointState::Succeeded)) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "state value is outside the known CheckpointState range",
    };
  }

  // Boundary: request_id, if present, must be non-empty.
  if (cp.request_id.has_value() && cp.request_id->empty()) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "request_id must be non-empty when present",
    };
  }

  // Boundary: goal_id, if present, must be non-empty.
  if (cp.goal_id.has_value() && cp.goal_id->empty()) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  // Boundary: belief_state_ref, if present, must be non-empty.
  if (cp.belief_state_ref.has_value() && cp.belief_state_ref->empty()) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "belief_state_ref must be non-empty when present",
    };
  }

  // Boundary: created_at, if present, must be positive.
  // Consistent with GoalContract (T004), AgentRequest (T002),
  // BeliefState (T009), ContextPacket (T010) timestamp rules.
  if (cp.created_at.has_value() && *cp.created_at <= 0) {
    return CheckpointGuardResult{
        .ok = false,
        .reason = "created_at must be a positive timestamp when present",
    };
  }

  return CheckpointGuardResult{
      .ok = true,
      .reason = "checkpoint boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation (WP03-T013-B).
//
// Validates WP03-T013 field rules on top of required + boundary checks:
//   1) All required + boundary checks (Layer 1 + Layer 2) are inherited.
//   2) tags, if present, must be a non-empty vector with no empty strings
//      (consistent with AgentRequest/GoalContract/BeliefState/ContextPacket
//      tags — unified cross-chain pattern).
//   3) State→pending_action semantic consistency (architecture §6.10):
//      - Paused, WaitingConfirm, WaitingTool states represent scenarios
//        where the agent is waiting for something specific.  In these
//        states, pending_action must be a non-empty string describing
//        what the agent is waiting for.
//      - Running, Failed, Succeeded states may have an empty
//        pending_action (no action is pending or failure occurred).
//
// Design reference: WP03-T013-D §5.3 Layer 3.
// ---------------------------------------------------------------------------
inline CheckpointGuardResult validate_checkpoint_field_rules(
    const Checkpoint& cp) {
  // Layer 1 + Layer 2: required + boundary checks (inherited).
  auto boundary_result = validate_checkpoint_boundary(cp);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  // -----------------------------------------------------------------------
  // O6-rule: tags — if present, must be non-empty vector with no empty
  // strings.  Consistent with AgentRequest/GoalContract/BeliefState/
  // ContextPacket tags (unified cross-chain pattern).
  // -----------------------------------------------------------------------
  if (cp.tags.has_value()) {
    if (cp.tags->empty()) {
      return CheckpointGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }
    for (const auto& tag : *cp.tags) {
      if (tag.empty()) {
        return CheckpointGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }
    }
  }

  // -----------------------------------------------------------------------
  // R2R5-rule: state→pending_action semantic consistency.
  //
  // Architecture §6.10 defines three mid-execution interruption scenarios
  // where the agent is waiting for something:
  //   - Paused        : waiting for user clarification (scenario 1).
  //   - WaitingConfirm: waiting for high-risk action confirmation (scenario 2).
  //   - WaitingTool   : waiting for async tool/sub-agent return (scenario 3).
  //
  // In these waiting states, pending_action must be a non-empty string
  // describing what is being waited for.  RecoveryManager (ADR-007 §5.2)
  // reads pending_action to determine recovery strategy; an empty value
  // would leave recovery without critical information.
  //
  // Running, Failed, Succeeded states may have an empty pending_action:
  //   - Running   : no outstanding side-effects yet.
  //   - Failed    : failure may not involve a pending action.
  //   - Succeeded : execution completed, nothing pending.
  // -----------------------------------------------------------------------
  if (cp.state.has_value()) {
    const auto s = *cp.state;
    const bool is_waiting_state =
        s == CheckpointState::Paused ||
        s == CheckpointState::WaitingConfirm ||
        s == CheckpointState::WaitingTool;

    if (is_waiting_state && cp.pending_action.has_value() &&
        cp.pending_action->empty()) {
      return CheckpointGuardResult{
          .ok = false,
          .reason = "pending_action must be non-empty for waiting states "
                    "(Paused/WaitingConfirm/WaitingTool)",
      };
    }
  }

  return CheckpointGuardResult{
      .ok = true,
      .reason = "checkpoint field rules validation passed",
  };
}

}  // namespace dasall::contracts
