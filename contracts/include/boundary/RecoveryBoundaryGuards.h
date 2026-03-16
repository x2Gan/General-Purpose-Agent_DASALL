#pragma once

#include <array>
#include <string_view>

namespace dasall::contracts {

// RecoveryBoundaryDecision normalizes the outcome of recovery-semantics
// boundary checks so contract tests and CI can assert a stable decision code.
enum class RecoveryBoundaryDecision {
  AllowField,
  RejectReflectionSchedulingField,
  RejectRecoveryAttributionField,
};

// RecoveryBoundaryResult carries a binary outcome plus a normalized decision
// and reason string to avoid duplicating rule mapping logic in tests.
struct RecoveryBoundaryResult {
  bool allowed = true;
  RecoveryBoundaryDecision decision = RecoveryBoundaryDecision::AllowField;
  std::string_view reason = "recovery boundary field is allowed by ADR-007";
};

// ADR-007 forbids ReflectionDecision from carrying runtime scheduling/control
// fields because ReflectionDecision is suggestion semantics only.
inline constexpr std::array<std::string_view, 5> kReflectionSchedulingForbiddenFields = {
    "retry_after_ms",
    "backoff_strategy",
    "lease_extension",
    "checkpoint_blob",
    "circuit_breaker_transition",
};

// WP01-T010 requires RecoveryOutcome to stay in execution-result semantics and
// reject failure-attribution style fields.
inline constexpr std::array<std::string_view, 4> kRecoveryAttributionForbiddenFields = {
    "failure_root_cause",
    "root_cause_analysis",
    "belief_patch",
    "plan_patch_hint",
};

// Evaluates candidate field names for ReflectionDecision.
constexpr RecoveryBoundaryResult evaluate_reflection_decision_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kReflectionSchedulingForbiddenFields) {
    if (field_name == forbidden_field) {
      return RecoveryBoundaryResult{
          .allowed = false,
          .decision = RecoveryBoundaryDecision::RejectReflectionSchedulingField,
          .reason = "reflection decision must not contain runtime scheduling fields",
      };
    }
  }

  return RecoveryBoundaryResult{};
}

// Evaluates candidate field names for RecoveryOutcome.
constexpr RecoveryBoundaryResult evaluate_recovery_outcome_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kRecoveryAttributionForbiddenFields) {
    if (field_name == forbidden_field) {
      return RecoveryBoundaryResult{
          .allowed = false,
          .decision = RecoveryBoundaryDecision::RejectRecoveryAttributionField,
          .reason = "recovery outcome must not contain failure attribution semantics",
      };
    }
  }

  return RecoveryBoundaryResult{};
}

// Boolean helpers for callers that only need pass/fail semantics.
constexpr bool is_allowed_reflection_decision_field(std::string_view field_name) {
  return evaluate_reflection_decision_field_boundary(field_name).allowed;
}

constexpr bool is_allowed_recovery_outcome_field(std::string_view field_name) {
  return evaluate_recovery_outcome_field_boundary(field_name).allowed;
}

}  // namespace dasall::contracts
