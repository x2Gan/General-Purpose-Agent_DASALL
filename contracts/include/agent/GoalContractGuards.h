#pragma once

#include <string_view>

#include "agent/GoalContract.h"
#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for GoalContract validation, following the same pattern as
// AgentRequestGuardResult (WP03-T002/T003).
// ---------------------------------------------------------------------------
struct GoalContractGuardResult {
  bool ok = false;
  std::string_view reason = "goal contract validation failed";
};

// ---------------------------------------------------------------------------
// Layer 1: Required-field presence validation (WP03-T004-B).
//
// Validates that all 6 required fields are present with meaningful values:
//   1) goal_id — present and non-empty.
//   2) request_id — present and non-empty.
//   3) goal_description — present and non-empty.
//   4) success_criteria — present and non-empty.
//   5) status — present and not Unspecified.
//   6) created_at — present and positive.
// ---------------------------------------------------------------------------
inline GoalContractGuardResult validate_goal_contract_required_fields(
    const GoalContract& goal) {
  if (!has_non_empty_value(goal.goal_id)) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "goal_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(goal.request_id)) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(goal.goal_description)) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "goal_description is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(goal.success_criteria)) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "success_criteria is required and must be non-empty",
    };
  }

  if (!goal.status.has_value() ||
      *goal.status == GoalStatus::Unspecified) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "status is required and must not be Unspecified",
    };
  }

  if (!goal.created_at.has_value() || *goal.created_at <= 0) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return GoalContractGuardResult{
      .ok = true,
      .reason = "all required fields present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary constraint validation (WP03-T004-B).
//
// Validates semantic boundary rules on top of required fields:
//   1) All required field checks (Layer 1).
//   2) status must be within the known GoalStatus enum range.
//   3) approval_policy, if present, must be within known ApprovalPolicy range.
//   4) deadline_at, if present, must be positive and >= created_at.
//   5) priority, if present, must be positive.
// ---------------------------------------------------------------------------
inline GoalContractGuardResult validate_goal_contract_boundary(
    const GoalContract& goal) {
  // Layer 1: required field presence.
  auto required_result = validate_goal_contract_required_fields(goal);
  if (!required_result.ok) {
    return required_result;
  }

  // Boundary: GoalStatus enum range (WP02-T012 unknown value guard).
  const int raw_status = static_cast<int>(*goal.status);
  if (raw_status < static_cast<int>(GoalStatus::Unspecified) ||
      raw_status > static_cast<int>(GoalStatus::Cancelled)) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "status value is outside the known GoalStatus range",
    };
  }

  // Boundary: ApprovalPolicy enum range, if present.
  if (goal.approval_policy.has_value()) {
    const int raw_policy = static_cast<int>(*goal.approval_policy);
    if (raw_policy < static_cast<int>(ApprovalPolicy::Unspecified) ||
        raw_policy > static_cast<int>(ApprovalPolicy::RequireConfirm)) {
      return GoalContractGuardResult{
          .ok = false,
          .reason =
              "approval_policy value is outside the known enum range",
      };
    }
    // Unspecified is allowed for optional approval_policy (means "not set").
  }

  // Boundary: deadline_at, if present, must be positive and >= created_at.
  if (goal.deadline_at.has_value()) {
    if (*goal.deadline_at <= 0) {
      return GoalContractGuardResult{
          .ok = false,
          .reason = "deadline_at must be a positive timestamp when present",
      };
    }
    if (goal.created_at.has_value() &&
        *goal.deadline_at < *goal.created_at) {
      return GoalContractGuardResult{
          .ok = false,
          .reason = "deadline_at must not be earlier than created_at",
      };
    }
  }

  // Boundary: priority, if present, must be positive.
  if (goal.priority.has_value() && *goal.priority == 0) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "priority must be positive when present",
    };
  }

  return GoalContractGuardResult{
      .ok = true,
      .reason = "goal contract boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation (WP03-T005-B).
//
// Validates WP03-T005 field rules on top of required + boundary checks:
//   1) All required + boundary checks (Layer 1 + Layer 2) are inherited.
//   2) Optional string fields (constraints, parent_goal_id), if present,
//      must be non-empty ("carry meaningful content or omit" per T003 §4.3).
//   3) tags, if present, must be a non-empty vector with no empty strings.
//   4) budget_override, if present, each non-nullopt dimension must be > 0
//      (reuses WP02-T007 RuntimeBudget 5-dimensional surface).
// ---------------------------------------------------------------------------
inline GoalContractGuardResult validate_goal_contract_field_rules(
    const GoalContract& goal) {
  // Layer 1 + Layer 2: required + boundary checks (WP03-T004 frozen).
  auto boundary_result = validate_goal_contract_boundary(goal);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  // Layer 3: optional string fields must not be present-but-empty.
  // An absent (nullopt) optional string is valid; a present empty string
  // violates the "carry meaningful content or omit" principle (T003 §4.3).
  if (goal.constraints.has_value() && goal.constraints->empty()) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "constraints must be non-empty when present",
    };
  }

  if (goal.parent_goal_id.has_value() && goal.parent_goal_id->empty()) {
    return GoalContractGuardResult{
        .ok = false,
        .reason = "parent_goal_id must be non-empty when present",
    };
  }

  // Layer 3: tags, if present, must be non-empty and contain no empty items.
  if (goal.tags.has_value()) {
    if (goal.tags->empty()) {
      return GoalContractGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }
    for (const auto& tag : *goal.tags) {
      if (tag.empty()) {
        return GoalContractGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }
    }
  }

  // Layer 3: budget_override dimensions, if present, must each be > 0.
  // Reuses WP02-T007 RuntimeBudget 5-dimensional surface, consistent
  // with AgentRequest.runtime_budget validation (T003-B).
  if (goal.budget_override.has_value()) {
    const auto& budget = *goal.budget_override;
    if (budget.max_tokens.has_value() && *budget.max_tokens == 0) {
      return GoalContractGuardResult{
          .ok = false,
          .reason =
              "budget_override.max_tokens must be positive when present",
      };
    }
    if (budget.max_turns.has_value() && *budget.max_turns == 0) {
      return GoalContractGuardResult{
          .ok = false,
          .reason =
              "budget_override.max_turns must be positive when present",
      };
    }
    if (budget.max_tool_calls.has_value() && *budget.max_tool_calls == 0) {
      return GoalContractGuardResult{
          .ok = false,
          .reason =
              "budget_override.max_tool_calls must be positive when present",
      };
    }
    if (budget.max_latency_ms.has_value() && *budget.max_latency_ms == 0) {
      return GoalContractGuardResult{
          .ok = false,
          .reason =
              "budget_override.max_latency_ms must be positive when present",
      };
    }
    if (budget.max_replan_count.has_value() &&
        *budget.max_replan_count == 0) {
      return GoalContractGuardResult{
          .ok = false,
          .reason =
              "budget_override.max_replan_count must be positive when present",
      };
    }
  }

  return GoalContractGuardResult{
      .ok = true,
      .reason = "goal contract field rules validation passed",
  };
}

}  // namespace dasall::contracts
