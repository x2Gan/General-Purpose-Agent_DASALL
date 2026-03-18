#pragma once

#include <cstddef>
#include <string_view>

#include "prompt/PromptComposeResult.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for PromptComposeResult validation.
// ---------------------------------------------------------------------------
struct PromptComposeResultGuardResult {
  bool ok = false;
  std::string_view reason = "prompt compose result validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP04-T004-B).
//
// Validates the 4 required PromptComposeResult fields frozen in T004-D:
//   1) messages            — present, non-empty vector, no empty elements
//   2) selected_prompt_id  — present and non-empty
//   3) selected_version    — present and non-empty
//   4) estimated_tokens    — present and non-negative
// ---------------------------------------------------------------------------
inline PromptComposeResultGuardResult
validate_prompt_compose_result_required_fields(
    const PromptComposeResult& result) {
  if (!result.messages.has_value() || result.messages->empty()) {
    return PromptComposeResultGuardResult{
        .ok = false,
        .reason = "messages are required and must contain at least one item",
    };
  }

  for (const auto& message : *result.messages) {
    if (message.empty()) {
      return PromptComposeResultGuardResult{
          .ok = false,
          .reason = "messages must not contain empty-string elements",
      };
    }
  }

  if (!result.selected_prompt_id.has_value() || result.selected_prompt_id->empty()) {
    return PromptComposeResultGuardResult{
        .ok = false,
        .reason = "selected_prompt_id is required and must be non-empty",
    };
  }

  if (!result.selected_version.has_value() || result.selected_version->empty()) {
    return PromptComposeResultGuardResult{
        .ok = false,
        .reason = "selected_version is required and must be non-empty",
    };
  }

  if (!result.estimated_tokens.has_value() || *result.estimated_tokens < 0) {
    return PromptComposeResultGuardResult{
        .ok = false,
        .reason = "estimated_tokens is required and must be non-negative",
    };
  }

  return PromptComposeResultGuardResult{
      .ok = true,
      .reason = "all required compose result fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary validation (WP04-T004-B).
//
// T004 freezes only the result object's semantic boundary, so this layer
// inherits all Layer-1 required checks and confirms the object is valid for
// downstream PromptPolicy / LLM adapter consumption.
//
// Memory/context write-back forbidden fields remain enforced via
// PromptBoundaryContracts.h field-name guards and are exercised in the
// contract tests rather than on the object instance itself.
// ---------------------------------------------------------------------------
inline PromptComposeResultGuardResult
validate_prompt_compose_result_boundary(const PromptComposeResult& result) {
  auto required_result = validate_prompt_compose_result_required_fields(result);
  if (!required_result.ok) {
    return required_result;
  }

  return PromptComposeResultGuardResult{
      .ok = true,
      .reason = "prompt compose result boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation (WP04-T005-B).
//
// Inherits all T004 required/boundary checks and then validates the optional
// metadata fields frozen by T005-D:
//   1) pruned_sections, if present, must be a non-empty vector.
//   2) pruned_sections must not contain empty-string elements.
//   3) pruned_sections carries audit-list semantics, so duplicate section ids
//      are forbidden.
//   4) composition_warnings, if present, must be a non-empty vector.
//   5) composition_warnings must not contain empty-string elements.
//
// Deliberately does not invent extra cross-field dependencies between
// pruned_sections and composition_warnings; T005 freezes only field-shape rules
// needed by PromptPolicy / LLMRequest handoff.
// ---------------------------------------------------------------------------
inline PromptComposeResultGuardResult
validate_prompt_compose_result_field_rules(const PromptComposeResult& result) {
  auto boundary_result = validate_prompt_compose_result_boundary(result);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (result.pruned_sections.has_value()) {
    if (result.pruned_sections->empty()) {
      return PromptComposeResultGuardResult{
          .ok = false,
          .reason = "pruned_sections must contain at least one item when present",
      };
    }

    for (const auto& section_id : *result.pruned_sections) {
      if (section_id.empty()) {
        return PromptComposeResultGuardResult{
            .ok = false,
            .reason = "pruned_sections must not contain empty-string elements",
        };
      }
    }

    for (std::size_t index = 0; index < result.pruned_sections->size(); ++index) {
      for (std::size_t probe = index + 1; probe < result.pruned_sections->size(); ++probe) {
        if ((*result.pruned_sections)[index] == (*result.pruned_sections)[probe]) {
          return PromptComposeResultGuardResult{
              .ok = false,
              .reason = "pruned_sections must not contain duplicate section identifiers",
          };
        }
      }
    }
  }

  if (result.composition_warnings.has_value()) {
    if (result.composition_warnings->empty()) {
      return PromptComposeResultGuardResult{
          .ok = false,
          .reason = "composition_warnings must contain at least one item when present",
      };
    }

    for (const auto& warning : *result.composition_warnings) {
      if (warning.empty()) {
        return PromptComposeResultGuardResult{
            .ok = false,
            .reason = "composition_warnings must not contain empty-string elements",
        };
      }
    }
  }

  return PromptComposeResultGuardResult{
      .ok = true,
      .reason = "prompt compose result field rules validation passed",
  };
}

}  // namespace dasall::contracts
