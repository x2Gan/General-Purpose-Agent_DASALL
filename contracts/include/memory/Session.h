#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// SessionBoundaryDecision classifies why a field name is not permitted in the
// stable Session contract surface.
enum class SessionBoundaryDecision {
  AllowField = 0,
  RejectSessionContextField = 1,
  RejectRuntimeControlField = 2,
  RejectCheckpointField = 3,
};

// SessionFieldBoundaryResult captures the decision made for a candidate field.
struct SessionFieldBoundaryResult {
  bool allowed = true;
  SessionBoundaryDecision decision = SessionBoundaryDecision::AllowField;
  std::string_view reason = "session field is allowed by WP05-T006";
};

// SessionGuardResult provides pass/fail output for Session validators.
struct SessionGuardResult {
  bool ok = false;
  std::string_view reason = "session validation failed";
};

// Session is the stable session index contract for the memory subsystem.
//
// Responsibility (WP05-T006 frozen):
//   - Represent a persisted session timeline.
//   - Keep the ordered set of turn identifiers belonging to the session.
//   - Point to the latest summary artifact and carry lightweight metadata.
//
// Session is NOT:
//   - NOT a runtime-owned SessionContext.
//   - NOT a top-level FSM or scheduling control object.
//   - NOT a Checkpoint or recovery admission object.
struct Session {
  // Stable session identifier reused across turns and memory lookups.
  std::optional<std::string> session_id;

  // Ordered Turn identifiers belonging to this session. The vector must be
  // present even for a newly opened session so consumers can distinguish “empty
  // session” from “missing timeline”.
  std::optional<std::vector<std::string>> turn_ids;

  // Optional stable user or principal identifier owning the session.
  std::optional<std::string> user_id;

  // Optional anchor to the latest structured summary generated for the session.
  std::optional<std::string> latest_summary_memory_ref;

  // Lightweight metadata digest for stable storage and retrieval. This field is
  // intentionally a digest, not a SessionContext dump.
  std::optional<std::string> metadata_digest;

  // Session creation timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Latest activity timestamp in milliseconds. Optional because a new session
  // may only have a creation time when first persisted.
  std::optional<std::int64_t> last_active_at;

  // Retrieval/audit tags for indexing and diagnostics.
  std::optional<std::vector<std::string>> tags;
};

// SessionContext-owned runtime fields that must not leak into Session.
inline constexpr std::array<std::string_view, 5>
    kSessionSessionContextForbiddenFields = {
        "active_goal",
        "skill_profile",
        "planner_state",
        "visible_tools",
        "policy_digest",
};

// Top-level execution-control fields that belong to runtime orchestration,
// rather than to the stable Session storage surface.
inline constexpr std::array<std::string_view, 4>
    kSessionRuntimeControlForbiddenFields = {
        "fsm_state",
        "retry_after_ms",
        "scheduler_slot",
        "recovery_action",
};

// Checkpoint-owned recovery fields that must not be duplicated by Session.
inline constexpr std::array<std::string_view, 4>
    kSessionCheckpointForbiddenFields = {
        "checkpoint_id",
        "pending_action",
        "working_memory_snapshot",
        "retry_count",
};

inline SessionGuardResult validate_session_required_fields(
    const Session& session) {
  if (!has_non_empty_value(session.session_id)) {
    return SessionGuardResult{
        .ok = false,
        .reason = "session_id is required and must be non-empty",
    };
  }

  if (!session.turn_ids.has_value()) {
    return SessionGuardResult{
        .ok = false,
        .reason = "turn_ids is required (use an empty vector for a new session)",
    };
  }

  if (!session.created_at.has_value() || *session.created_at <= 0) {
    return SessionGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return SessionGuardResult{
      .ok = true,
      .reason = "all required session fields present",
  };
}

inline SessionGuardResult validate_session_boundary(const Session& session) {
  const auto required_result = validate_session_required_fields(session);
  if (!required_result.ok) {
    return required_result;
  }

  if (session.last_active_at.has_value() &&
      *session.last_active_at < *session.created_at) {
    return SessionGuardResult{
        .ok = false,
        .reason = "last_active_at must not be earlier than created_at",
    };
  }

  if (session.latest_summary_memory_ref.has_value() &&
      session.latest_summary_memory_ref->empty()) {
    return SessionGuardResult{
        .ok = false,
        .reason = "latest_summary_memory_ref must be non-empty when present",
    };
  }

  return SessionGuardResult{
      .ok = true,
      .reason = "session boundary validation passed",
  };
}

// validate_session_field_rules applies the field-level rules that keep Session
// compact, non-duplicated, and storage-oriented.
inline SessionGuardResult validate_session_field_rules(const Session& session) {
  const auto boundary_result = validate_session_boundary(session);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  for (const auto& turn_id : *session.turn_ids) {
    if (turn_id.empty()) {
      return SessionGuardResult{
          .ok = false,
          .reason = "turn_ids must not contain empty strings",
      };
    }
  }

  for (std::size_t index = 0; index < session.turn_ids->size(); ++index) {
    for (std::size_t probe = index + 1; probe < session.turn_ids->size();
         ++probe) {
      if ((*session.turn_ids)[index] == (*session.turn_ids)[probe]) {
        return SessionGuardResult{
            .ok = false,
            .reason = "turn_ids must not contain duplicate items",
        };
      }
    }
  }

  if (session.user_id.has_value() && session.user_id->empty()) {
    return SessionGuardResult{
        .ok = false,
        .reason = "user_id must be non-empty when present",
    };
  }

  if (session.metadata_digest.has_value() && session.metadata_digest->empty()) {
    return SessionGuardResult{
        .ok = false,
        .reason = "metadata_digest must be non-empty when present",
    };
  }

  if (session.tags.has_value()) {
    if (session.tags->empty()) {
      return SessionGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < session.tags->size(); ++index) {
      if ((*session.tags)[index].empty()) {
        return SessionGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < session.tags->size(); ++probe) {
        if ((*session.tags)[index] == (*session.tags)[probe]) {
          return SessionGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return SessionGuardResult{
      .ok = true,
      .reason = "session field rules validation passed",
  };
}

constexpr SessionFieldBoundaryResult evaluate_session_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kSessionSessionContextForbiddenFields) {
    if (field_name == forbidden_field) {
      return SessionFieldBoundaryResult{
          .allowed = false,
          .decision = SessionBoundaryDecision::RejectSessionContextField,
          .reason = "session must not contain SessionContext-owned fields",
      };
    }
  }

  for (const auto forbidden_field : kSessionRuntimeControlForbiddenFields) {
    if (field_name == forbidden_field) {
      return SessionFieldBoundaryResult{
          .allowed = false,
          .decision = SessionBoundaryDecision::RejectRuntimeControlField,
          .reason = "session must not contain runtime control or FSM fields",
      };
    }
  }

  for (const auto forbidden_field : kSessionCheckpointForbiddenFields) {
    if (field_name == forbidden_field) {
      return SessionFieldBoundaryResult{
          .allowed = false,
          .decision = SessionBoundaryDecision::RejectCheckpointField,
          .reason = "session must not contain checkpoint or recovery fields",
      };
    }
  }

  return SessionFieldBoundaryResult{};
}

inline SessionGuardResult validate_session_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_session_field_boundary(field_name);
  return SessionGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts