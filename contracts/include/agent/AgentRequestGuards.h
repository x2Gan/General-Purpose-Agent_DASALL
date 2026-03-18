#pragma once

#include <string_view>

#include "agent/AgentRequest.h"
#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// Guard result for AgentRequest validation, following the same pattern as
// IdentityMetadataGuardResult and TimeDeadlineGuardResult.
struct AgentRequestGuardResult {
  bool ok = false;
  std::string_view reason = "agent request validation failed";
};

// Validates WP03-T002/T003 frozen required-field rules:
//   1) request_id, session_id, trace_id must be present and non-empty.
//   2) user_input must be present and non-empty.
//   3) request_channel must be present and not Unspecified.
//   4) created_at must be present and positive.
inline AgentRequestGuardResult validate_agent_request_required_fields(
    const AgentRequest& request) {
  if (!has_non_empty_value(request.request_id)) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.session_id)) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "session_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.trace_id)) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "trace_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.user_input)) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "user_input is required and must be non-empty",
    };
  }

  if (!request.request_channel.has_value() ||
      *request.request_channel == RequestChannel::Unspecified) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "request_channel is required and must not be Unspecified",
    };
  }

  if (!request.created_at.has_value() || *request.created_at <= 0) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return AgentRequestGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// Validates WP03-T002 semantic boundary constraints on top of required fields:
//   1) request_channel must be within the known enum range (WP02-T012).
//   2) created_at must not be negative.
//   3) If deadline_at is present, it must be positive and >= created_at.
//   4) All checks from validate_agent_request_required_fields are included.
inline AgentRequestGuardResult validate_agent_request_boundary(
    const AgentRequest& request) {
  // First pass: required field presence.
  auto required_result = validate_agent_request_required_fields(request);
  if (!required_result.ok) {
    return required_result;
  }

  // Boundary: request_channel enum range (WP02-T012 unknown value guard).
  const int raw_channel = static_cast<int>(*request.request_channel);
  if (raw_channel < static_cast<int>(RequestChannel::Unspecified) ||
      raw_channel > static_cast<int>(RequestChannel::Simulator)) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "request_channel value is outside the known enum range",
    };
  }

  // Boundary: deadline_at, if present, must be positive and >= created_at.
  if (request.deadline_at.has_value()) {
    if (*request.deadline_at <= 0) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason = "deadline_at must be a positive timestamp when present",
      };
    }
    if (request.created_at.has_value() &&
        *request.deadline_at < *request.created_at) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason = "deadline_at must not be earlier than created_at",
      };
    }
  }

  return AgentRequestGuardResult{
      .ok = true,
      .reason = "agent request boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// WP03-T003-B: Field-level validation
// ---------------------------------------------------------------------------
// Validates WP03-T003 field rules on top of required + boundary checks:
//   1) Optional string fields, if present, must be non-empty (goal_hint,
//      domain_context, constraint_set, approval_policy_hint, idempotency_key,
//      locale, client_capabilities).
//   2) timeout_ms, if present, must be positive.
//   3) priority, if present, must be positive.
//   4) tags, if present, must contain no empty strings.
//   5) runtime_budget, if present, each non-nullopt dimension must be > 0.
//   6) All required + boundary checks from prior guards are included.
inline AgentRequestGuardResult validate_agent_request_field_rules(
    const AgentRequest& request) {
  // Layer 1: required + boundary checks (WP03-T002).
  auto boundary_result = validate_agent_request_boundary(request);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  // Layer 2: optional string fields must not be present-but-empty.
  // An absent (nullopt) optional string is valid; a present empty string
  // violates the "carry meaningful content or omit" principle (T003 §4.3).
  if (request.goal_hint.has_value() && request.goal_hint->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "goal_hint must be non-empty when present",
    };
  }
  if (request.domain_context.has_value() && request.domain_context->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "domain_context must be non-empty when present",
    };
  }
  if (request.constraint_set.has_value() && request.constraint_set->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "constraint_set must be non-empty when present",
    };
  }
  if (request.approval_policy_hint.has_value() &&
      request.approval_policy_hint->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "approval_policy_hint must be non-empty when present",
    };
  }
  if (request.idempotency_key.has_value() &&
      request.idempotency_key->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "idempotency_key must be non-empty when present",
    };
  }
  if (request.locale.has_value() && request.locale->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "locale must be non-empty when present",
    };
  }
  if (request.client_capabilities.has_value() &&
      request.client_capabilities->empty()) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "client_capabilities must be non-empty when present",
    };
  }

  // Layer 3: optional numeric fields must be positive when present.
  if (request.timeout_ms.has_value() && *request.timeout_ms == 0) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "timeout_ms must be positive when present",
    };
  }
  if (request.priority.has_value() && *request.priority == 0) {
    return AgentRequestGuardResult{
        .ok = false,
        .reason = "priority must be positive when present",
    };
  }

  // Layer 4: tags, if present, must be non-empty and contain no empty items.
  if (request.tags.has_value()) {
    if (request.tags->empty()) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }
    for (const auto& tag : *request.tags) {
      if (tag.empty()) {
        return AgentRequestGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }
    }
  }

  // Layer 5: runtime_budget dimensions, if present, must each be > 0.
  if (request.runtime_budget.has_value()) {
    const auto& budget = *request.runtime_budget;
    if (budget.max_tokens.has_value() && *budget.max_tokens == 0) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_tokens must be positive when present",
      };
    }
    if (budget.max_turns.has_value() && *budget.max_turns == 0) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_turns must be positive when present",
      };
    }
    if (budget.max_tool_calls.has_value() && *budget.max_tool_calls == 0) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason =
              "runtime_budget.max_tool_calls must be positive when present",
      };
    }
    if (budget.max_latency_ms.has_value() && *budget.max_latency_ms == 0) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason =
              "runtime_budget.max_latency_ms must be positive when present",
      };
    }
    if (budget.max_replan_count.has_value() &&
        *budget.max_replan_count == 0) {
      return AgentRequestGuardResult{
          .ok = false,
          .reason =
              "runtime_budget.max_replan_count must be positive when present",
      };
    }
  }

  return AgentRequestGuardResult{
      .ok = true,
      .reason = "agent request field rules validation passed",
  };
}

}  // namespace dasall::contracts
