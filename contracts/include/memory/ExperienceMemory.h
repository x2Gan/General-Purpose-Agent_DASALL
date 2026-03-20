#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// ExperienceMemoryBoundaryDecision classifies why a candidate field is outside
// the frozen ExperienceMemory boundary.
enum class ExperienceMemoryBoundaryDecision {
  AllowField = 0,
  RejectRuntimeControlField = 1,
  RejectPromptProviderField = 2,
  RejectCheckpointField = 3,
};

// ExperienceMemoryFieldBoundaryResult reports whether a candidate field is
// accepted by the ExperienceMemory contract surface.
struct ExperienceMemoryFieldBoundaryResult {
  bool allowed = true;
  ExperienceMemoryBoundaryDecision decision =
      ExperienceMemoryBoundaryDecision::AllowField;
  std::string_view reason = "experience memory field is allowed by WP05-T007";
};

// ExperienceMemoryGuardResult provides pass/fail output for validators.
struct ExperienceMemoryGuardResult {
  bool ok = false;
  std::string_view reason = "experience memory validation failed";
};

// ExperienceMemory is the stable lesson-oriented memory contract used for
// write-back loops and long-term adaptation.
//
// Responsibility (WP05-T007 frozen):
//   - Persist one reusable lesson learned from completed executions.
//   - Bind lesson items to source fact/turn references.
//   - Preserve applicability and confidence metadata for safe reuse.
//
// ExperienceMemory is NOT:
//   - NOT a runtime retry/recovery scheduler.
//   - NOT a prompt rendering or provider parameter container.
//   - NOT a checkpoint snapshot carrier.
struct ExperienceMemory {
  // Stable identifier for one persisted experience item.
  std::optional<std::string> experience_id;

  // Session identifier that produced this lesson.
  std::optional<std::string> session_id;

  // Human-readable lesson summary that can be reused across turns.
  std::optional<std::string> lesson_summary;

  // Trigger condition that explains when this lesson applies.
  std::optional<std::string> trigger_condition;

  // Recommended action when the trigger condition is met.
  std::optional<std::string> recommended_action;

  // Experience persistence timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Optional source fact references that support this lesson.
  std::optional<std::vector<std::string>> source_fact_ids;

  // Optional source turn references for audit and traceability.
  std::optional<std::vector<std::string>> source_turn_ids;

  // Optional effectiveness score in [0, 100].
  std::optional<std::uint32_t> effectiveness_score;

  // Optional domain tags indicating where this lesson is applicable.
  std::optional<std::vector<std::string>> applicable_domains;

  // Optional risk notes for guarded usage.
  std::optional<std::string> risk_notes;

  // Optional expiration timestamp in milliseconds.
  std::optional<std::int64_t> expires_at;

  // Optional successor experience id when this lesson is superseded.
  std::optional<std::string> superseded_by_experience_id;

  // Retrieval/audit tags for indexing and diagnostics.
  std::optional<std::vector<std::string>> tags;
};

// Runtime control fields that must never leak into ExperienceMemory.
inline constexpr std::array<std::string_view, 4>
    kExperienceMemoryRuntimeControlForbiddenFields = {
        "fsm_state",
        "retry_after_ms",
        "recovery_action",
        "scheduler_slot",
};

// Prompt/provider fields that belong to prompt and llm subdomains.
inline constexpr std::array<std::string_view, 4>
    kExperienceMemoryPromptProviderForbiddenFields = {
        "rendered_prompt",
        "provider_payload",
        "model_route",
        "temperature",
};

// Checkpoint-owned fields that must remain in recovery contracts.
inline constexpr std::array<std::string_view, 4>
    kExperienceMemoryCheckpointForbiddenFields = {
        "checkpoint_id",
        "pending_action",
        "working_memory_snapshot",
        "retry_count",
};

inline ExperienceMemoryGuardResult validate_experience_memory_required_fields(
    const ExperienceMemory& experience) {
  if (!has_non_empty_value(experience.experience_id)) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "experience_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(experience.session_id)) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "session_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(experience.lesson_summary)) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "lesson_summary is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(experience.trigger_condition)) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "trigger_condition is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(experience.recommended_action)) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "recommended_action is required and must be non-empty",
    };
  }

  if (!experience.created_at.has_value() || *experience.created_at <= 0) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return ExperienceMemoryGuardResult{
      .ok = true,
      .reason = "all required experience memory fields present",
  };
}

inline ExperienceMemoryGuardResult validate_experience_memory_boundary(
    const ExperienceMemory& experience) {
  const auto required_result =
      validate_experience_memory_required_fields(experience);
  if (!required_result.ok) {
    return required_result;
  }

  if (experience.expires_at.has_value() &&
      *experience.expires_at <= *experience.created_at) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "expires_at must be later than created_at when present",
    };
  }

  if (experience.effectiveness_score.has_value() &&
      *experience.effectiveness_score > 100) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "effectiveness_score must be in [0, 100] when present",
    };
  }

  if (experience.superseded_by_experience_id.has_value() &&
      experience.superseded_by_experience_id->empty()) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "superseded_by_experience_id must be non-empty when present",
    };
  }

  return ExperienceMemoryGuardResult{
      .ok = true,
      .reason = "experience memory boundary validation passed",
  };
}

// validate_experience_memory_field_rules enforces deduplication and non-empty
// optional values to keep lesson write-back contracts predictable.
inline ExperienceMemoryGuardResult validate_experience_memory_field_rules(
    const ExperienceMemory& experience) {
  const auto boundary_result = validate_experience_memory_boundary(experience);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  const auto validate_optional_string_vector = [](
      const std::optional<std::vector<std::string>>& values,
      std::string_view empty_reason,
      std::string_view duplicate_reason) -> ExperienceMemoryGuardResult {
    if (!values.has_value()) {
      return ExperienceMemoryGuardResult{.ok = true, .reason = "optional vector absent"};
    }

    if (values->empty()) {
      return ExperienceMemoryGuardResult{.ok = false, .reason = empty_reason};
    }

    for (std::size_t index = 0; index < values->size(); ++index) {
      if ((*values)[index].empty()) {
        return ExperienceMemoryGuardResult{
            .ok = false,
            .reason = "experience memory vectors must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < values->size(); ++probe) {
        if ((*values)[index] == (*values)[probe]) {
          return ExperienceMemoryGuardResult{
              .ok = false,
              .reason = duplicate_reason,
          };
        }
      }
    }

    return ExperienceMemoryGuardResult{.ok = true, .reason = "vector validation passed"};
  };

  const auto fact_ids_result = validate_optional_string_vector(
      experience.source_fact_ids,
      "source_fact_ids must contain at least one item when present",
      "source_fact_ids must not contain duplicate items");
  if (!fact_ids_result.ok) {
    return fact_ids_result;
  }

  const auto turn_ids_result = validate_optional_string_vector(
      experience.source_turn_ids,
      "source_turn_ids must contain at least one item when present",
      "source_turn_ids must not contain duplicate items");
  if (!turn_ids_result.ok) {
    return turn_ids_result;
  }

  const auto domains_result = validate_optional_string_vector(
      experience.applicable_domains,
      "applicable_domains must contain at least one item when present",
      "applicable_domains must not contain duplicate items");
  if (!domains_result.ok) {
    return domains_result;
  }

  const auto tags_result = validate_optional_string_vector(
      experience.tags,
      "tags must contain at least one item when present",
      "tags must not contain duplicate items");
  if (!tags_result.ok) {
    return tags_result;
  }

  if (experience.risk_notes.has_value() && experience.risk_notes->empty()) {
    return ExperienceMemoryGuardResult{
        .ok = false,
        .reason = "risk_notes must be non-empty when present",
    };
  }

  return ExperienceMemoryGuardResult{
      .ok = true,
      .reason = "experience memory field rules validation passed",
  };
}

constexpr ExperienceMemoryFieldBoundaryResult
evaluate_experience_memory_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field :
       kExperienceMemoryRuntimeControlForbiddenFields) {
    if (field_name == forbidden_field) {
      return ExperienceMemoryFieldBoundaryResult{
          .allowed = false,
          .decision =
              ExperienceMemoryBoundaryDecision::RejectRuntimeControlField,
          .reason = "experience memory must not contain runtime control fields",
      };
    }
  }

  for (const auto forbidden_field :
       kExperienceMemoryPromptProviderForbiddenFields) {
    if (field_name == forbidden_field) {
      return ExperienceMemoryFieldBoundaryResult{
          .allowed = false,
          .decision =
              ExperienceMemoryBoundaryDecision::RejectPromptProviderField,
          .reason = "experience memory must not contain prompt/provider fields",
      };
    }
  }

  for (const auto forbidden_field : kExperienceMemoryCheckpointForbiddenFields) {
    if (field_name == forbidden_field) {
      return ExperienceMemoryFieldBoundaryResult{
          .allowed = false,
          .decision = ExperienceMemoryBoundaryDecision::RejectCheckpointField,
          .reason = "experience memory must not contain checkpoint fields",
      };
    }
  }

  return ExperienceMemoryFieldBoundaryResult{};
}

}  // namespace dasall::contracts