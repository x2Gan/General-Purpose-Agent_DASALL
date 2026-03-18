#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "prompt/PromptComposeRequest.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for PromptComposeRequest validation.
//
// Follows the chain-wide guard result pattern established by
// AgentRequestGuardResult (WP03-T002/T003) and GoalContractGuardResult
// (WP03-T004/T005).  Using std::string_view avoids heap allocation and
// keeps guard functions usable in constexpr contexts.
// ---------------------------------------------------------------------------
struct PromptComposeRequestGuardResult {
  bool ok = false;
  std::string_view reason = "prompt compose request validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP04-T002-B).
//
// Validates that all 4 required fields are present with meaningful values:
//   1) request_id        — present and non-empty (WP02-T009 traceability).
//   2) stage             — present and not Unspecified (ADR-006 §6.2).
//   3) context_packet_id — present and non-empty (ADR-006 §6.2).
//   4) created_at        — present and positive (WP02-T010).
//
// Design reference: WP04-T002-D §2.2 / Layer-1 invariant from
// GoalContractGuards.h (WP03-T004-B).
// ---------------------------------------------------------------------------
inline PromptComposeRequestGuardResult
validate_prompt_compose_request_required_fields(
    const PromptComposeRequest& req) {
  // R1: request_id — present and non-empty.
  if (!has_non_empty_value(req.request_id)) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  // R2: stage — present and not Unspecified.
  // Unspecified stage means PromptRegistry has no basis for PromptSpec selection.
  if (!req.stage.has_value() ||
      *req.stage == CompositionStage::Unspecified) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "stage is required and must not be Unspecified",
    };
  }

  // R3: context_packet_id — present and non-empty.
  // PromptComposer must be able to resolve the ContextPacket; without this
  // reference, no semantically grounded Prompt can be assembled.
  if (!has_non_empty_value(req.context_packet_id)) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "context_packet_id is required and must be non-empty",
    };
  }

  // R4: created_at — present and positive.
  if (!req.created_at.has_value() || *req.created_at <= 0) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return PromptComposeRequestGuardResult{
      .ok     = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP04-T002-B).
//
// Validates semantic boundary rules on top of required fields:
//   1) All required field checks (Layer 1).
//   2) stage must be within the known CompositionStage enum range
//      (WP02-T012 unknown-value guard).
//
// Design reference: WP04-T002-D §2.2 boundary rules / WP02-T012 enum guard /
// AgentRequestGuards.h boundary pattern (WP03-T002-B).
// ---------------------------------------------------------------------------
inline PromptComposeRequestGuardResult
validate_prompt_compose_request_boundary(const PromptComposeRequest& req) {
  // Layer 1: required field presence.
  auto required_result = validate_prompt_compose_request_required_fields(req);
  if (!required_result.ok) {
    return required_result;
  }

  // Boundary: stage enum range (WP02-T012 unknown-value guard).
  // Valid values are Planning(1) through Response(4); 0 is Unspecified
  // (already rejected by Layer 1) and values > 4 are out of range.
  const int raw_stage = static_cast<int>(*req.stage);
  if (raw_stage < static_cast<int>(CompositionStage::Planning) ||
      raw_stage > static_cast<int>(CompositionStage::Response)) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "stage value is outside the known CompositionStage enum range",
    };
  }

  return PromptComposeRequestGuardResult{
      .ok     = true,
      .reason = "prompt compose request boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation (WP04-T002-B).
//
// Validates WP04-T002-D §2.3 optional-field rules on top of required and
// boundary checks:
//   1) All required + boundary checks (Layer 1 + Layer 2) are inherited.
//   2) Optional string fields, if present, must be non-empty.
//      ("carry meaningful content or omit" — WP03-T003 §4.3)
//   3) visible_tools, if present, must be a non-empty vector with
//      no empty-string elements.
//   4) tags, if present, must be a non-empty vector with no empty-string
//      elements (consistent with GoalContractGuards Layer 3 tags rule).
//   5) request_id and context_packet_id must match to preserve the
//      one-request-one-context binding declared in WP04-T002-D §1.2.
//   6) visible_tools has set semantics; duplicate tool ids are forbidden.
//
// Design reference: WP04-T002-D §2.3 / GoalContractGuards.h Layer 3
// (WP03-T005-B).
// ---------------------------------------------------------------------------
inline PromptComposeRequestGuardResult
validate_prompt_compose_request_field_rules(const PromptComposeRequest& req) {
  // Layer 1 + Layer 2: required + boundary checks.
  auto boundary_result = validate_prompt_compose_request_boundary(req);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  // Layer 3 combination rule C1: request_id and context_packet_id must match.
  // Both are guaranteed present and non-empty by Layer 1. A mismatch means
  // the compose request is no longer bound to the current request's unique
  // ContextPacket, which would detach PromptComposer from the active context
  // chain defined in WP04-T002-D §1.2.
  if (*req.request_id != *req.context_packet_id) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "context_packet_id must equal request_id for the active request context",
    };
  }

  // Layer 3: optional string fields must not be present-but-empty.
  if (req.task_type.has_value() && req.task_type->empty()) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "task_type must be non-empty when present",
    };
  }

  if (req.prompt_release_id.has_value() && req.prompt_release_id->empty()) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "prompt_release_id must be non-empty when present",
    };
  }

  if (req.model_route.has_value() && req.model_route->empty()) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "model_route must be non-empty when present",
    };
  }

  if (req.output_schema_ref.has_value() && req.output_schema_ref->empty()) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "output_schema_ref must be non-empty when present",
    };
  }

  if (req.response_format.has_value() && req.response_format->empty()) {
    return PromptComposeRequestGuardResult{
        .ok     = false,
        .reason = "response_format must be non-empty when present",
    };
  }

  // Layer 3: visible_tools, if present, must be non-empty and contain no
  // empty-string elements.
  if (req.visible_tools.has_value()) {
    if (req.visible_tools->empty()) {
      return PromptComposeRequestGuardResult{
          .ok     = false,
          .reason = "visible_tools must contain at least one item when present",
      };
    }
    for (const auto& tool_id : *req.visible_tools) {
      if (tool_id.empty()) {
        return PromptComposeRequestGuardResult{
            .ok     = false,
            .reason = "visible_tools must not contain empty-string elements",
        };
      }
    }

    // Layer 3 combination rule C2: visible_tools is a set of tool ids.
    // Duplicate entries would cause PromptComposer to inject the same tool
    // definition multiple times, so the field must remain unique.
    for (std::size_t index = 0; index < req.visible_tools->size(); ++index) {
      for (std::size_t probe = index + 1; probe < req.visible_tools->size(); ++probe) {
        if ((*req.visible_tools)[index] == (*req.visible_tools)[probe]) {
          return PromptComposeRequestGuardResult{
              .ok     = false,
              .reason = "visible_tools must not contain duplicate tool identifiers",
          };
        }
      }
    }
  }

  // Layer 3: tags, if present, must be non-empty and contain no empty-string
  // elements.  Consistent with GoalContract, AgentRequest, ContextPacket tags
  // rules.
  if (req.tags.has_value()) {
    if (req.tags->empty()) {
      return PromptComposeRequestGuardResult{
          .ok     = false,
          .reason = "tags must contain at least one item when present",
      };
    }
    for (const auto& tag : *req.tags) {
      if (tag.empty()) {
        return PromptComposeRequestGuardResult{
            .ok     = false,
            .reason = "tags must not contain empty-string elements",
        };
      }
    }
  }

  return PromptComposeRequestGuardResult{
      .ok     = true,
      .reason = "prompt compose request field rules validation passed",
  };
}

}  // namespace dasall::contracts
