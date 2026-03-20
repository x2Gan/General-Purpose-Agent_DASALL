#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// TurnBoundaryDecision classifies the category of boundary violation detected
// when a field name is evaluated against the frozen WP05-T006 Turn boundary.
enum class TurnBoundaryDecision {
  AllowField = 0,
  RejectCheckpointField = 1,
  RejectSessionContextField = 2,
  RejectExecutionRecordField = 3,
};

// TurnFieldBoundaryResult reports whether a candidate field name belongs to the
// allowed Turn contract surface.
struct TurnFieldBoundaryResult {
  bool allowed = true;
  TurnBoundaryDecision decision = TurnBoundaryDecision::AllowField;
  std::string_view reason = "turn field is allowed by WP05-T006";
};

// TurnGuardResult provides a simple pass/fail result for required-field,
// boundary, and field-level validation layers.
struct TurnGuardResult {
  bool ok = false;
  std::string_view reason = "turn validation failed";
};

// Turn is the stable single-round session record for memory contracts.
//
// Responsibility (WP05-T006 frozen):
//   - Record the user input of one round.
//   - Keep lightweight references to tool calls and observations produced in
//     that round.
//   - Optionally persist the final assistant response and the summary anchor
//     generated after the round.
//
// Turn is NOT:
//   - NOT a Checkpoint or recovery snapshot.
//   - NOT a ContextPacket or SessionContext assembly object.
//   - NOT an Observation/ToolResult raw payload container.
struct Turn {
  // Stable identifier for one persisted round inside a session timeline.
  std::optional<std::string> turn_id;

  // Owning session identifier. Reuses the stable session correlation surface
  // without embedding SessionContext internals.
  std::optional<std::string> session_id;

  // Raw user-side input for the round. This is the semantic entry content for
  // the turn, not the final prompt message bundle.
  std::optional<std::string> user_input;

  // Assistant-facing final response for the round. Optional because the turn
  // record may be persisted before a final response is available.
  std::optional<std::string> agent_response;

  // Stable tool-call identifiers or digest references produced in this round.
  // The Turn object stores references only, never full ToolRequest/ToolResult.
  std::optional<std::vector<std::string>> tool_call_refs;

  // Stable observation identifiers or digest references captured in this round.
  // These references let memory correlate with Observation without owning the
  // raw execution payload.
  std::optional<std::vector<std::string>> observation_refs;

  // Optional anchor to the summary generated from or after this round.
  std::optional<std::string> summary_memory_ref;

  // Turn persistence timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Retrieval/audit tags for memory indexing and diagnostics.
  std::optional<std::vector<std::string>> tags;
};

// Frozen forbidden fields for Checkpoint ownership. Turn can reference round
// artifacts, but it must not absorb recovery state.
inline constexpr std::array<std::string_view, 5>
    kTurnCheckpointForbiddenFields = {
        "checkpoint_id",
        "state",
        "pending_action",
        "working_memory_snapshot",
        "retry_count",
};

// Frozen forbidden fields for SessionContext/ContextPacket ownership. Turn is a
// stable stored record, not an assembled runtime context object.
inline constexpr std::array<std::string_view, 5>
    kTurnSessionContextForbiddenFields = {
        "current_goal_summary",
        "recent_history",
        "policy_digest",
        "token_budget_report",
        "belief_state_summary",
};

// Frozen forbidden fields for execution-record ownership. Turn can point to
// observations, but must not embed raw execution payloads.
inline constexpr std::array<std::string_view, 4>
    kTurnExecutionRecordForbiddenFields = {
        "payload",
        "error",
        "side_effects",
        "provider_payload",
};

inline TurnGuardResult validate_turn_required_fields(const Turn& turn) {
  if (!has_non_empty_value(turn.turn_id)) {
    return TurnGuardResult{
        .ok = false,
        .reason = "turn_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(turn.session_id)) {
    return TurnGuardResult{
        .ok = false,
        .reason = "session_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(turn.user_input)) {
    return TurnGuardResult{
        .ok = false,
        .reason = "user_input is required and must be non-empty",
    };
  }

  if (!turn.created_at.has_value() || *turn.created_at <= 0) {
    return TurnGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return TurnGuardResult{
      .ok = true,
      .reason = "all required turn fields present",
  };
}

inline TurnGuardResult validate_turn_boundary(const Turn& turn) {
  const auto required_result = validate_turn_required_fields(turn);
  if (!required_result.ok) {
    return required_result;
  }

  if (turn.summary_memory_ref.has_value() &&
      turn.summary_memory_ref->empty()) {
    return TurnGuardResult{
        .ok = false,
        .reason = "summary_memory_ref must be non-empty when present",
    };
  }

  return TurnGuardResult{
      .ok = true,
      .reason = "turn boundary validation passed",
  };
}

// validate_turn_field_rules applies the WP05-T006 field-level rules on top of
// required-field and boundary validation.
inline TurnGuardResult validate_turn_field_rules(const Turn& turn) {
  const auto boundary_result = validate_turn_boundary(turn);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (turn.agent_response.has_value() && turn.agent_response->empty()) {
    return TurnGuardResult{
        .ok = false,
        .reason = "agent_response must be non-empty when present",
    };
  }

  if (turn.tool_call_refs.has_value()) {
    if (turn.tool_call_refs->empty()) {
      return TurnGuardResult{
          .ok = false,
          .reason = "tool_call_refs must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < turn.tool_call_refs->size(); ++index) {
      if ((*turn.tool_call_refs)[index].empty()) {
        return TurnGuardResult{
            .ok = false,
            .reason = "tool_call_refs must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < turn.tool_call_refs->size();
           ++probe) {
        if ((*turn.tool_call_refs)[index] == (*turn.tool_call_refs)[probe]) {
          return TurnGuardResult{
              .ok = false,
              .reason = "tool_call_refs must not contain duplicate items",
          };
        }
      }
    }
  }

  if (turn.observation_refs.has_value()) {
    if (turn.observation_refs->empty()) {
      return TurnGuardResult{
          .ok = false,
          .reason = "observation_refs must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < turn.observation_refs->size(); ++index) {
      if ((*turn.observation_refs)[index].empty()) {
        return TurnGuardResult{
            .ok = false,
            .reason = "observation_refs must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1;
           probe < turn.observation_refs->size();
           ++probe) {
        if ((*turn.observation_refs)[index] ==
            (*turn.observation_refs)[probe]) {
          return TurnGuardResult{
              .ok = false,
              .reason = "observation_refs must not contain duplicate items",
          };
        }
      }
    }
  }

  if (turn.tags.has_value()) {
    if (turn.tags->empty()) {
      return TurnGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < turn.tags->size(); ++index) {
      if ((*turn.tags)[index].empty()) {
        return TurnGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < turn.tags->size(); ++probe) {
        if ((*turn.tags)[index] == (*turn.tags)[probe]) {
          return TurnGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return TurnGuardResult{
      .ok = true,
      .reason = "turn field rules validation passed",
  };
}

constexpr TurnFieldBoundaryResult evaluate_turn_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kTurnCheckpointForbiddenFields) {
    if (field_name == forbidden_field) {
      return TurnFieldBoundaryResult{
          .allowed = false,
          .decision = TurnBoundaryDecision::RejectCheckpointField,
          .reason = "turn must not contain checkpoint or recovery fields",
      };
    }
  }

  for (const auto forbidden_field : kTurnSessionContextForbiddenFields) {
    if (field_name == forbidden_field) {
      return TurnFieldBoundaryResult{
          .allowed = false,
          .decision = TurnBoundaryDecision::RejectSessionContextField,
          .reason = "turn must not contain session-context or context-packet assembly fields",
      };
    }
  }

  for (const auto forbidden_field : kTurnExecutionRecordForbiddenFields) {
    if (field_name == forbidden_field) {
      return TurnFieldBoundaryResult{
          .allowed = false,
          .decision = TurnBoundaryDecision::RejectExecutionRecordField,
          .reason = "turn must not contain raw execution payload fields",
      };
    }
  }

  return TurnFieldBoundaryResult{};
}

inline TurnGuardResult validate_turn_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_turn_field_boundary(field_name);
  return TurnGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts