#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// SummaryMemoryBoundaryDecision classifies the category of boundary violation
// for SummaryMemory field evaluation.
enum class SummaryMemoryBoundaryDecision {
  AllowField = 0,
  RejectSessionContextField = 1,
  RejectCheckpointField = 2,
  RejectExecutionRecordField = 3,
};

// SummaryMemoryFieldBoundaryResult reports whether a field name is compatible
// with the frozen SummaryMemory contract surface.
struct SummaryMemoryFieldBoundaryResult {
  bool allowed = true;
  SummaryMemoryBoundaryDecision decision =
      SummaryMemoryBoundaryDecision::AllowField;
  std::string_view reason = "summary memory field is allowed by WP05-T006";
};

// SummaryMemoryGuardResult provides the pass/fail result for validators.
struct SummaryMemoryGuardResult {
  bool ok = false;
  std::string_view reason = "summary memory validation failed";
};

// SummaryMemory is the structured summary artifact persisted by the memory
// subsystem after compression or consolidation.
//
// Responsibility (WP05-T006 frozen):
//   - Persist the summary text for a session snapshot.
//   - Carry structured high-value deposits such as decisions, confirmed facts,
//     and tool outcomes.
//   - Point back to the source turns that were compressed.
//
// SummaryMemory is NOT:
//   - NOT a ContextPacket or SessionContext assembly object.
//   - NOT a Checkpoint or recovery state carrier.
//   - NOT a raw execution record or BeliefState replacement.
struct SummaryMemory {
  // Stable identifier for the persisted summary artifact.
  std::optional<std::string> summary_id;

  // Session identifier owning this summary artifact.
  std::optional<std::string> session_id;

  // Human-readable compressed summary text that can later be projected into
  // ContextPacket.summary_memory.
  std::optional<std::string> summary_text;

  // Turn identifiers covered by this summary artifact.
  std::optional<std::vector<std::string>> source_turn_ids;

  // High-level decisions that should survive long-term compression.
  std::optional<std::vector<std::string>> decisions_made;

  // Stable facts confirmed during the covered turns.
  std::optional<std::vector<std::string>> confirmed_facts;

  // Tool-result summaries worth carrying forward after compression.
  std::optional<std::vector<std::string>> tool_outcomes;

  // Summary creation timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Retrieval/audit tags for indexing and diagnostics.
  std::optional<std::vector<std::string>> tags;
};

// Context assembly fields that belong to ContextPacket or SessionContext rather
// than to the persisted SummaryMemory object.
inline constexpr std::array<std::string_view, 5>
    kSummaryMemorySessionContextForbiddenFields = {
        "current_goal_summary",
        "recent_history",
        "policy_digest",
        "token_budget_report",
        "belief_state_summary",
};

// Checkpoint-owned recovery fields that must never be duplicated by
// SummaryMemory.
inline constexpr std::array<std::string_view, 5>
    kSummaryMemoryCheckpointForbiddenFields = {
        "checkpoint_id",
        "state",
        "pending_action",
        "working_memory_snapshot",
        "retry_count",
};

// Raw execution-record fields that SummaryMemory must reference only via
// textual outcome summaries, never by embedding the original payloads.
inline constexpr std::array<std::string_view, 4>
    kSummaryMemoryExecutionRecordForbiddenFields = {
        "payload",
        "error",
        "side_effects",
        "provider_payload",
};

inline SummaryMemoryGuardResult validate_summary_memory_required_fields(
    const SummaryMemory& memory) {
  if (!has_non_empty_value(memory.summary_id)) {
    return SummaryMemoryGuardResult{
        .ok = false,
        .reason = "summary_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(memory.session_id)) {
    return SummaryMemoryGuardResult{
        .ok = false,
        .reason = "session_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(memory.summary_text)) {
    return SummaryMemoryGuardResult{
        .ok = false,
        .reason = "summary_text is required and must be non-empty",
    };
  }

  if (!memory.created_at.has_value() || *memory.created_at <= 0) {
    return SummaryMemoryGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return SummaryMemoryGuardResult{
      .ok = true,
      .reason = "all required summary memory fields present",
  };
}

inline SummaryMemoryGuardResult validate_summary_memory_boundary(
    const SummaryMemory& memory) {
  const auto required_result = validate_summary_memory_required_fields(memory);
  if (!required_result.ok) {
    return required_result;
  }

  return SummaryMemoryGuardResult{
      .ok = true,
      .reason = "summary memory boundary validation passed",
  };
}

// validate_summary_memory_field_rules enforces the structured-summary hygiene
// needed to keep SummaryMemory compact, stable, and evolvable.
inline SummaryMemoryGuardResult validate_summary_memory_field_rules(
    const SummaryMemory& memory) {
  const auto boundary_result = validate_summary_memory_boundary(memory);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  const auto validate_optional_string_vector = [](
      const std::optional<std::vector<std::string>>& values,
      std::string_view empty_reason,
      std::string_view duplicate_reason) -> SummaryMemoryGuardResult {
    if (!values.has_value()) {
      return SummaryMemoryGuardResult{
          .ok = true,
          .reason = "optional vector absent",
      };
    }

    if (values->empty()) {
      return SummaryMemoryGuardResult{
          .ok = false,
          .reason = empty_reason,
      };
    }

    for (std::size_t index = 0; index < values->size(); ++index) {
      if ((*values)[index].empty()) {
        return SummaryMemoryGuardResult{
            .ok = false,
            .reason = "summary memory vectors must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < values->size(); ++probe) {
        if ((*values)[index] == (*values)[probe]) {
          return SummaryMemoryGuardResult{
              .ok = false,
              .reason = duplicate_reason,
          };
        }
      }
    }

    return SummaryMemoryGuardResult{
        .ok = true,
        .reason = "optional vector validation passed",
    };
  };

  const auto source_turn_result = validate_optional_string_vector(
      memory.source_turn_ids,
      "source_turn_ids must contain at least one item when present",
      "source_turn_ids must not contain duplicate items");
  if (!source_turn_result.ok) {
    return source_turn_result;
  }

  const auto decisions_result = validate_optional_string_vector(
      memory.decisions_made,
      "decisions_made must contain at least one item when present",
      "decisions_made must not contain duplicate items");
  if (!decisions_result.ok) {
    return decisions_result;
  }

  const auto facts_result = validate_optional_string_vector(
      memory.confirmed_facts,
      "confirmed_facts must contain at least one item when present",
      "confirmed_facts must not contain duplicate items");
  if (!facts_result.ok) {
    return facts_result;
  }

  const auto outcomes_result = validate_optional_string_vector(
      memory.tool_outcomes,
      "tool_outcomes must contain at least one item when present",
      "tool_outcomes must not contain duplicate items");
  if (!outcomes_result.ok) {
    return outcomes_result;
  }

  const auto tags_result = validate_optional_string_vector(
      memory.tags,
      "tags must contain at least one item when present",
      "tags must not contain duplicate items");
  if (!tags_result.ok) {
    return tags_result;
  }

  return SummaryMemoryGuardResult{
      .ok = true,
      .reason = "summary memory field rules validation passed",
  };
}

constexpr SummaryMemoryFieldBoundaryResult
evaluate_summary_memory_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kSummaryMemorySessionContextForbiddenFields) {
    if (field_name == forbidden_field) {
      return SummaryMemoryFieldBoundaryResult{
          .allowed = false,
          .decision = SummaryMemoryBoundaryDecision::RejectSessionContextField,
          .reason = "summary memory must not contain session-context or context-packet assembly fields",
      };
    }
  }

  for (const auto forbidden_field : kSummaryMemoryCheckpointForbiddenFields) {
    if (field_name == forbidden_field) {
      return SummaryMemoryFieldBoundaryResult{
          .allowed = false,
          .decision = SummaryMemoryBoundaryDecision::RejectCheckpointField,
          .reason = "summary memory must not contain checkpoint or recovery fields",
      };
    }
  }

  for (const auto forbidden_field : kSummaryMemoryExecutionRecordForbiddenFields) {
    if (field_name == forbidden_field) {
      return SummaryMemoryFieldBoundaryResult{
          .allowed = false,
          .decision = SummaryMemoryBoundaryDecision::RejectExecutionRecordField,
          .reason = "summary memory must not contain raw execution payload fields",
      };
    }
  }

  return SummaryMemoryFieldBoundaryResult{};
}

inline SummaryMemoryGuardResult validate_summary_memory_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_summary_memory_field_boundary(field_name);
  return SummaryMemoryGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts