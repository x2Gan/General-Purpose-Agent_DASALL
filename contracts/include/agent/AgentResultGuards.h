#pragma once

#include <string_view>

#include "agent/AgentResult.h"
#include "boundary/GuardCommon.h"
#include "error/ResultCode.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for AgentResult validation, following the same pattern as
// CheckpointGuardResult (WP03-T012), BeliefStateGuardResult (WP03-T009),
// and GoalContractGuardResult (WP03-T004).
// ---------------------------------------------------------------------------
struct AgentResultGuardResult {
  bool ok = false;
  std::string_view reason = "agent result validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T014-B).
//
// Validates that all 6 required fields are present with meaningful values:
//   R1) result_id       — present and non-empty.
//   R2) status          — present and not Unspecified.
//   R3) result_code     — present (any value accepted; range checked in L2).
//   R4) response_text   — present (empty string allowed; structured-output
//                          scenarios may not produce text).
//   R5) task_completed  — present (true or false).
//   R6) created_at      — present and positive.
//
// Design reference: WP03-T014-D §5.8 Layer 1.
// ---------------------------------------------------------------------------
inline AgentResultGuardResult validate_agent_result_required_fields(
    const AgentResult& result) {
  // R1: result_id must be present and non-empty.
  if (!has_non_empty_value(result.result_id)) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "result_id is required and must be non-empty",
    };
  }

  // R2: status must be present and not Unspecified.
  if (!result.status.has_value() ||
      *result.status == AgentResultStatus::Unspecified) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "status is required and must not be Unspecified",
    };
  }

  // R3: result_code must be present.
  if (!result.result_code.has_value()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "result_code is required",
    };
  }

  // R4: response_text must be present (has_value).
  // Empty string is allowed — structured-output-only scenarios may
  // produce no text reply.  Nullopt means the field was never set.
  if (!result.response_text.has_value()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason =
            "response_text is required (use empty string if no text reply)",
    };
  }

  // R5: task_completed must be present.
  if (!result.task_completed.has_value()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "task_completed is required",
    };
  }

  // R6: created_at must be present and positive.
  if (!result.created_at.has_value() || *result.created_at <= 0) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return AgentResultGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T014-B).
//
// Validates semantic boundary rules on top of required fields:
//   1) All required field checks (Layer 1).
//   2) status must be within the known AgentResultStatus enum range.
//   3) result_code must be within WP-02 frozen category ranges
//      (classify_result_code_segment != Unknown).
//   4) request_id, if present, must be non-empty.
//   5) trace_id, if present, must be non-empty.
//   6) structured_payload, if present, must be non-empty.
//   7) checkpoint_ref, if present, must be non-empty.
//   8) goal_id, if present, must be non-empty.
//   9) tags, if present, must be non-empty vector with no empty strings.
//
// Design reference: WP03-T014-D §5.8 Layer 2.
// ---------------------------------------------------------------------------
inline AgentResultGuardResult validate_agent_result_boundary(
    const AgentResult& result) {
  // Layer 1: required field presence.
  auto required_result = validate_agent_result_required_fields(result);
  if (!required_result.ok) {
    return required_result;
  }

  // Boundary: AgentResultStatus enum range check.
  // Values must be between Completed (1) and Timeout (5) inclusive,
  // since Unspecified (0) is already rejected by Layer 1.
  const int raw_status = static_cast<int>(*result.status);
  if (raw_status < static_cast<int>(AgentResultStatus::Unspecified) ||
      raw_status > static_cast<int>(AgentResultStatus::Timeout)) {
    return AgentResultGuardResult{
        .ok = false,
        .reason =
            "status value is outside the known AgentResultStatus range",
    };
  }

  // Boundary: result_code must be within WP-02 frozen category ranges.
  // classify_result_code_segment returns Unknown for out-of-range values.
  if (classify_result_code_segment(*result.result_code) ==
      ResultCodeCategory::Unknown) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "result_code is outside WP-02 frozen category ranges "
                  "(1000-5999)",
    };
  }

  // Boundary: request_id, if present, must be non-empty.
  if (result.request_id.has_value() && result.request_id->empty()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "request_id must be non-empty when present",
    };
  }

  // Boundary: trace_id, if present, must be non-empty.
  if (result.trace_id.has_value() && result.trace_id->empty()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "trace_id must be non-empty when present",
    };
  }

  // Boundary: structured_payload, if present, must be non-empty.
  if (result.structured_payload.has_value() &&
      result.structured_payload->empty()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "structured_payload must be non-empty when present",
    };
  }

  // Boundary: checkpoint_ref, if present, must be non-empty.
  if (result.checkpoint_ref.has_value() &&
      result.checkpoint_ref->empty()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "checkpoint_ref must be non-empty when present",
    };
  }

  // Boundary: goal_id, if present, must be non-empty.
  if (result.goal_id.has_value() && result.goal_id->empty()) {
    return AgentResultGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  // Boundary: tags, if present, must be non-empty vector with no empty
  // strings.  Consistent with AgentRequest/GoalContract/BeliefState/
  // ContextPacket/Checkpoint tags (unified cross-chain pattern).
  if (result.tags.has_value()) {
    if (result.tags->empty()) {
      return AgentResultGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }
    for (const auto& tag : *result.tags) {
      if (tag.empty()) {
        return AgentResultGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }
    }
  }

  return AgentResultGuardResult{
      .ok = true,
      .reason = "agent result boundary validation passed",
  };
}

}  // namespace dasall::contracts
