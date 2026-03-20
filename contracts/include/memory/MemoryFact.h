#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// MemoryFactBoundaryDecision classifies why a candidate field is outside the
// frozen MemoryFact boundary.
enum class MemoryFactBoundaryDecision {
  AllowField = 0,
  RejectSessionContextField = 1,
  RejectRuntimeControlField = 2,
  RejectProviderPayloadField = 3,
};

// MemoryFactFieldBoundaryResult reports whether a candidate field is accepted
// by the MemoryFact contract surface.
struct MemoryFactFieldBoundaryResult {
  bool allowed = true;
  MemoryFactBoundaryDecision decision = MemoryFactBoundaryDecision::AllowField;
  std::string_view reason = "memory fact field is allowed by WP05-T007";
};

// MemoryFactGuardResult provides pass/fail output for MemoryFact validation.
struct MemoryFactGuardResult {
  bool ok = false;
  std::string_view reason = "memory fact validation failed";
};

// MemoryFact is the stable atomic fact contract written by memory pipelines.
//
// Responsibility (WP05-T007 frozen):
//   - Persist one confirmed fact that can be reused in future turns.
//   - Keep references to source turns/observations for traceability.
//   - Preserve confidence and temporal metadata for controlled write-back.
//
// MemoryFact is NOT:
//   - NOT a SessionContext or ContextPacket runtime assembly object.
//   - NOT a runtime controller for retry/fsm/scheduling decisions.
//   - NOT a provider payload or prompt transcript container.
struct MemoryFact {
  // Stable identifier for one persisted fact item.
  std::optional<std::string> fact_id;

  // Session identifier owning this fact.
  std::optional<std::string> session_id;

  // Human-readable normalized fact statement.
  std::optional<std::string> fact_text;

  // Source turn identifiers that support this fact.
  std::optional<std::vector<std::string>> source_turn_ids;

  // Confidence score in [0, 100] for deterministic storage checks.
  std::optional<std::uint32_t> confidence_score;

  // Fact persistence timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Optional category of the fact, such as preference or constraint.
  std::optional<std::string> fact_type;

  // Optional observation references supporting this fact.
  std::optional<std::vector<std::string>> source_observation_refs;

  // Optional validity end timestamp in milliseconds.
  std::optional<std::int64_t> valid_until;

  // Optional successor fact id when this fact has been superseded.
  std::optional<std::string> superseded_by_fact_id;

  // Optional digest for evidence audit trails.
  std::optional<std::string> evidence_digest;

  // Retrieval/audit tags for indexing and diagnostics.
  std::optional<std::vector<std::string>> tags;
};

// Runtime context fields that belong to SessionContext/ContextPacket rather
// than to MemoryFact.
inline constexpr std::array<std::string_view, 4>
    kMemoryFactSessionContextForbiddenFields = {
        "recent_history",
        "current_goal_summary",
        "policy_digest",
        "belief_state_summary",
};

// Runtime control fields that must never leak into MemoryFact.
inline constexpr std::array<std::string_view, 4>
    kMemoryFactRuntimeControlForbiddenFields = {
        "fsm_state",
        "retry_count",
        "pending_action",
        "scheduler_slot",
};

// Provider-owned payload fields that must stay outside shared contracts.
inline constexpr std::array<std::string_view, 4>
    kMemoryFactProviderPayloadForbiddenFields = {
        "provider_payload",
        "rendered_prompt",
        "raw_embedding",
        "token_logprobs",
};

inline MemoryFactGuardResult validate_memory_fact_required_fields(
    const MemoryFact& fact) {
  if (!has_non_empty_value(fact.fact_id)) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "fact_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(fact.session_id)) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "session_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(fact.fact_text)) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "fact_text is required and must be non-empty",
    };
  }

  if (!fact.source_turn_ids.has_value() || fact.source_turn_ids->empty()) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "source_turn_ids is required and must contain at least one item",
    };
  }

  if (!fact.confidence_score.has_value() || *fact.confidence_score > 100) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "confidence_score is required and must be in [0, 100]",
    };
  }

  if (!fact.created_at.has_value() || *fact.created_at <= 0) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return MemoryFactGuardResult{
      .ok = true,
      .reason = "all required memory fact fields present",
  };
}

inline MemoryFactGuardResult validate_memory_fact_boundary(const MemoryFact& fact) {
  const auto required_result = validate_memory_fact_required_fields(fact);
  if (!required_result.ok) {
    return required_result;
  }

  if (fact.valid_until.has_value() && *fact.valid_until <= *fact.created_at) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "valid_until must be later than created_at when present",
    };
  }

  if (fact.superseded_by_fact_id.has_value() && fact.superseded_by_fact_id->empty()) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "superseded_by_fact_id must be non-empty when present",
    };
  }

  return MemoryFactGuardResult{
      .ok = true,
      .reason = "memory fact boundary validation passed",
  };
}

// validate_memory_fact_field_rules enforces deduplication and string hygiene
// so MemoryFact stays compact and stable for long-term schema evolution.
inline MemoryFactGuardResult validate_memory_fact_field_rules(
    const MemoryFact& fact) {
  const auto boundary_result = validate_memory_fact_boundary(fact);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  const auto validate_optional_string_vector = [](
      const std::optional<std::vector<std::string>>& values,
      std::string_view required_reason,
      std::string_view duplicate_reason,
      bool required) -> MemoryFactGuardResult {
    if (!values.has_value()) {
      if (!required) {
        return MemoryFactGuardResult{.ok = true, .reason = "optional vector absent"};
      }
      return MemoryFactGuardResult{.ok = false, .reason = required_reason};
    }

    if (values->empty()) {
      return MemoryFactGuardResult{.ok = false, .reason = required_reason};
    }

    for (std::size_t index = 0; index < values->size(); ++index) {
      if ((*values)[index].empty()) {
        return MemoryFactGuardResult{
            .ok = false,
            .reason = "memory fact vectors must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < values->size(); ++probe) {
        if ((*values)[index] == (*values)[probe]) {
          return MemoryFactGuardResult{.ok = false, .reason = duplicate_reason};
        }
      }
    }

    return MemoryFactGuardResult{.ok = true, .reason = "vector validation passed"};
  };

  const auto source_turns_result = validate_optional_string_vector(
      fact.source_turn_ids,
      "source_turn_ids is required and must contain at least one item",
      "source_turn_ids must not contain duplicate items",
      true);
  if (!source_turns_result.ok) {
    return source_turns_result;
  }

  const auto source_observation_result = validate_optional_string_vector(
      fact.source_observation_refs,
      "source_observation_refs must contain at least one item when present",
      "source_observation_refs must not contain duplicate items",
      false);
  if (!source_observation_result.ok) {
    return source_observation_result;
  }

  const auto tags_result = validate_optional_string_vector(
      fact.tags,
      "tags must contain at least one item when present",
      "tags must not contain duplicate items",
      false);
  if (!tags_result.ok) {
    return tags_result;
  }

  if (fact.fact_type.has_value() && fact.fact_type->empty()) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "fact_type must be non-empty when present",
    };
  }

  if (fact.evidence_digest.has_value() && fact.evidence_digest->empty()) {
    return MemoryFactGuardResult{
        .ok = false,
        .reason = "evidence_digest must be non-empty when present",
    };
  }

  return MemoryFactGuardResult{
      .ok = true,
      .reason = "memory fact field rules validation passed",
  };
}

constexpr MemoryFactFieldBoundaryResult evaluate_memory_fact_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kMemoryFactSessionContextForbiddenFields) {
    if (field_name == forbidden_field) {
      return MemoryFactFieldBoundaryResult{
          .allowed = false,
          .decision = MemoryFactBoundaryDecision::RejectSessionContextField,
          .reason = "memory fact must not contain SessionContext/ContextPacket fields",
      };
    }
  }

  for (const auto forbidden_field : kMemoryFactRuntimeControlForbiddenFields) {
    if (field_name == forbidden_field) {
      return MemoryFactFieldBoundaryResult{
          .allowed = false,
          .decision = MemoryFactBoundaryDecision::RejectRuntimeControlField,
          .reason = "memory fact must not contain runtime control fields",
      };
    }
  }

  for (const auto forbidden_field : kMemoryFactProviderPayloadForbiddenFields) {
    if (field_name == forbidden_field) {
      return MemoryFactFieldBoundaryResult{
          .allowed = false,
          .decision = MemoryFactBoundaryDecision::RejectProviderPayloadField,
          .reason = "memory fact must not contain provider payload fields",
      };
    }
  }

  return MemoryFactFieldBoundaryResult{};
}

}  // namespace dasall::contracts