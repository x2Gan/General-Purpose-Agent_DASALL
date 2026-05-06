#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "context/ContextPacket.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for ContextPacket field validation, following the same
// pattern as GoalContractGuardResult (WP03-T004/T005) and
// AgentRequestGuardResult (WP03-T002/T003).
// ---------------------------------------------------------------------------
struct ContextPacketGuardResult {
  bool ok = false;
  std::string_view reason = "context packet validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T011-B).
//
// Validates that all 4 required fields are present with meaningful values:
//   1) request_id   — present and non-empty (traceability to AgentRequest).
//   2) user_turn    — present and non-empty (ADR-006 §6.1 slot 1).
//   3) current_goal_summary — present and non-empty (ADR-006 §6.1 slot 2).
//   4) recent_history — present (may be empty vector on first turn,
//                       ADR-006 §6.1 slot 3).
//
// Design reference: WP03-T011-D §5.1 / §5.3 Layer 1.
// ---------------------------------------------------------------------------
inline ContextPacketGuardResult validate_context_packet_required_fields(
    const ContextPacket& pkt) {
  // R1: request_id — present and non-empty.
  if (!has_non_empty_value(pkt.request_id)) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  // R2: user_turn — present and non-empty.
  if (!has_non_empty_value(pkt.user_turn)) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "user_turn is required and must be non-empty",
    };
  }

  // R3: current_goal_summary — present and non-empty.
  if (!has_non_empty_value(pkt.current_goal_summary)) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "current_goal_summary is required and must be non-empty",
    };
  }

  // R4: recent_history — must be present (has_value).
  // An empty vector is valid (first turn), but nullopt means missing.
  if (!pkt.recent_history.has_value()) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "recent_history is required (use empty vector for first turn)",
    };
  }

  return ContextPacketGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T011-B).
//
// Validates semantic boundary rules on top of required fields:
//   1) All required field checks (Layer 1).
//   2) created_at, if present, must be positive (> 0).
//
// Design reference: WP03-T011-D §5.3 Layer 2.
// ---------------------------------------------------------------------------
inline ContextPacketGuardResult validate_context_packet_boundary(
    const ContextPacket& pkt) {
  // Layer 1: required field presence.
  auto required_result = validate_context_packet_required_fields(pkt);
  if (!required_result.ok) {
    return required_result;
  }

  // O8-boundary: created_at, if present, must be positive.
  // Consistent with GoalContract (T004), AgentRequest (T002),
  // BeliefState (T009) timestamp rules.
  if (pkt.created_at.has_value() && *pkt.created_at <= 0) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "created_at must be a positive timestamp when present",
    };
  }

  return ContextPacketGuardResult{
      .ok = true,
      .reason = "context packet boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation (WP03-T011-B).
//
// Validates WP03-T011 field rules on top of required + boundary checks:
//   1) All required + boundary checks (Layer 1 + Layer 2) are inherited.
//   2) Optional string fields (summary_memory,
//      latest_observation_digest_summary, policy_digest,
//      token_budget_report, belief_state_summary), if present,
//      must be non-empty ("carry meaningful content or omit",
//      per WP03-T003 §4.3).
//   3) Optional vector fields (retrieval_evidence, retrieval_evidence_refs,
//      active_tools), if present, must be non-empty and structurally valid.
//   4) tags, if present, must be a non-empty vector with no empty strings
//      (consistent with AgentRequest/GoalContract/BeliefState tags).
//
// Design reference: WP03-T011-D §5.2 / §5.3 Layer 3.
// ---------------------------------------------------------------------------
inline ContextPacketGuardResult validate_context_packet_field_rules(
    const ContextPacket& pkt) {
  // Layer 1 + Layer 2: required + boundary checks (inherited).
  auto boundary_result = validate_context_packet_boundary(pkt);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  // -----------------------------------------------------------------------
  // O1-rule: summary_memory — if present, must be non-empty.
  // -----------------------------------------------------------------------
  if (pkt.summary_memory.has_value() && pkt.summary_memory->empty()) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "summary_memory must be non-empty when present",
    };
  }

  // -----------------------------------------------------------------------
  // O2-rule: retrieval_evidence — if present, must be non-empty vector
  // with no empty strings.
  // -----------------------------------------------------------------------
  if (pkt.retrieval_evidence.has_value()) {
    if (pkt.retrieval_evidence->empty()) {
      return ContextPacketGuardResult{
          .ok = false,
          .reason =
              "retrieval_evidence must contain at least one item when present",
      };
    }
    for (const auto& item : *pkt.retrieval_evidence) {
      if (item.empty()) {
        return ContextPacketGuardResult{
            .ok = false,
            .reason =
                "retrieval_evidence must not contain empty strings",
        };
      }
    }
  }

  // -----------------------------------------------------------------------
  // O2b-rule: retrieval_evidence_refs — if present, must be non-empty and
  // each ref must satisfy RetrievalEvidenceRef::has_consistent_values().
  // -----------------------------------------------------------------------
  if (pkt.retrieval_evidence_refs.has_value()) {
    if (pkt.retrieval_evidence_refs->empty()) {
      return ContextPacketGuardResult{
          .ok = false,
          .reason =
              "retrieval_evidence_refs must contain at least one item when present",
      };
    }
    for (const auto& ref : *pkt.retrieval_evidence_refs) {
      if (!ref.has_consistent_values()) {
        return ContextPacketGuardResult{
            .ok = false,
            .reason =
                "retrieval_evidence_refs must contain only consistent evidence refs",
        };
      }
    }
  }

  // -----------------------------------------------------------------------
  // O3-rule: latest_observation_digest_summary — if present, non-empty.
  // -----------------------------------------------------------------------
  if (pkt.latest_observation_digest_summary.has_value() &&
      pkt.latest_observation_digest_summary->empty()) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason =
            "latest_observation_digest_summary must be non-empty when present",
    };
  }

  // -----------------------------------------------------------------------
  // O4-rule: active_tools — if present, must be non-empty vector with
  // no empty strings.
  // -----------------------------------------------------------------------
  if (pkt.active_tools.has_value()) {
    if (pkt.active_tools->empty()) {
      return ContextPacketGuardResult{
          .ok = false,
          .reason =
              "active_tools must contain at least one item when present",
      };
    }
    for (const auto& tool : *pkt.active_tools) {
      if (tool.empty()) {
        return ContextPacketGuardResult{
            .ok = false,
            .reason = "active_tools must not contain empty strings",
        };
      }
    }
  }

  // -----------------------------------------------------------------------
  // O5-rule: policy_digest — if present, must be non-empty.
  // -----------------------------------------------------------------------
  if (pkt.policy_digest.has_value() && pkt.policy_digest->empty()) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "policy_digest must be non-empty when present",
    };
  }

  // -----------------------------------------------------------------------
  // O6-rule: token_budget_report — if present, must be non-empty.
  // -----------------------------------------------------------------------
  if (pkt.token_budget_report.has_value() &&
      pkt.token_budget_report->empty()) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "token_budget_report must be non-empty when present",
    };
  }

  // -----------------------------------------------------------------------
  // O7-rule: belief_state_summary — if present, must be non-empty.
  // -----------------------------------------------------------------------
  if (pkt.belief_state_summary.has_value() &&
      pkt.belief_state_summary->empty()) {
    return ContextPacketGuardResult{
        .ok = false,
        .reason = "belief_state_summary must be non-empty when present",
    };
  }

  // -----------------------------------------------------------------------
  // O9-rule: tags — if present, must be non-empty vector with no empty
  // strings. Consistent with AgentRequest/GoalContract/BeliefState tags.
  // -----------------------------------------------------------------------
  if (pkt.tags.has_value()) {
    if (pkt.tags->empty()) {
      return ContextPacketGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }
    for (const auto& tag : *pkt.tags) {
      if (tag.empty()) {
        return ContextPacketGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }
    }
  }

  return ContextPacketGuardResult{
      .ok = true,
      .reason = "context packet field rules validation passed",
  };
}

}  // namespace dasall::contracts
