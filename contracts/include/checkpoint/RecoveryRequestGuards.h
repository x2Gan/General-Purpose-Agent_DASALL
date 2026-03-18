#pragma once

#include <array>
#include <string_view>

#include "checkpoint/BudgetSnapshotGuards.h"
#include "checkpoint/CheckpointGuards.h"
#include "checkpoint/RecoveryRequest.h"
#include "checkpoint/ReflectionDecisionGuards.h"
#include "error/ErrorInfoGuards.h"
#include "observation/ObservationGuards.h"
#include "observation/ObservationSourceGuards.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// Guard result for RecoveryRequest validation.
//
// T009 validates two layers only:
//   1. Required nested evidence is present and structurally valid.
//   2. Boundary rules preserve ADR-007 semantics and prevent RecoveryRequest
//      from collapsing into a second ReflectionDecision or a premature
//      RecoveryOutcome.
// ---------------------------------------------------------------------------
struct RecoveryRequestGuardResult {
  bool ok = false;
  std::string_view reason = "recovery request validation failed";
};

enum class RecoveryRequestBoundaryDecision {
  AllowField,
  RejectReflectionTopLevelField,
  RejectOutcomeField,
  RejectExecutionSchedulingField,
};

struct RecoveryRequestBoundaryResult {
  bool allowed = true;
  RecoveryRequestBoundaryDecision decision =
      RecoveryRequestBoundaryDecision::AllowField;
  std::string_view reason = "recovery request field is allowed";
};

inline constexpr std::array<std::string_view, 5>
    kRecoveryRequestReflectionTopLevelForbiddenFields = {
        "decision_kind",
        "rationale",
        "confidence",
        "relevant_observation_refs",
        "hint_ref",
    };

inline constexpr std::array<std::string_view, 7>
    kRecoveryRequestOutcomeForbiddenFields = {
        "executed_action",
        "final_runtime_state",
        "updated_retry_counters",
        "checkpoint_ref",
        "compensation_result_ref",
        "rejection_reason",
        "escalation_reason",
    };

inline constexpr std::array<std::string_view, 3>
    kRecoveryRequestExecutionSchedulingForbiddenFields = {
        "retry_after_ms",
        "backoff_strategy",
        "circuit_breaker_transition",
    };

inline RecoveryRequestGuardResult
validate_idempotency_and_side_effect_report_required_fields(
    const IdempotencyAndSideEffectReport& report) {
  if (!report.replay_safe.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "idempotency_and_side_effect_report.replay_safe is required",
    };
  }

  if (report.idempotency_key.has_value() && report.idempotency_key->empty()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "idempotency_and_side_effect_report.idempotency_key must be non-empty when present",
    };
  }

  if (!report.side_effects_present.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "idempotency_and_side_effect_report.side_effects_present is required",
    };
  }

  if (!*report.replay_safe) {
    if (!report.non_replayable_reason.has_value() ||
        report.non_replayable_reason->empty()) {
      return RecoveryRequestGuardResult{
          .ok = false,
          .reason =
              "non_replayable_reason is required when replay_safe is false",
      };
    }
  }

  if (*report.replay_safe && report.non_replayable_reason.has_value() &&
      !report.non_replayable_reason->empty()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "non_replayable_reason must be empty when replay_safe is true",
    };
  }

  return RecoveryRequestGuardResult{
      .ok = true,
      .reason = "idempotency and side-effect report is valid",
  };
}

inline RecoveryRequestGuardResult
validate_idempotency_and_side_effect_report_field_rules(
    const IdempotencyAndSideEffectReport& report) {
  const auto required_result =
      validate_idempotency_and_side_effect_report_required_fields(report);
  if (!required_result.ok) {
    return required_result;
  }

  if (*report.replay_safe && *report.side_effects_present &&
      (!report.idempotency_key.has_value() || report.idempotency_key->empty())) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "idempotency_key is required when replay_safe is true and side_effects_present is true",
    };
  }

  return RecoveryRequestGuardResult{
      .ok = true,
      .reason = "idempotency and side-effect report field rules passed",
  };
}

inline bool recovery_request_optional_string_values_match(
    const std::optional<std::string>& left,
    const std::optional<std::string>& right) {
  return !left.has_value() || !right.has_value() || *left == *right;
}

inline bool recovery_request_error_info_matches(
    const ErrorInfo& left,
    const ErrorInfo& right) {
  return left.failure_type == right.failure_type &&
         left.retryable == right.retryable &&
         left.safe_to_replan == right.safe_to_replan &&
         left.details.code == right.details.code &&
         left.details.message == right.details.message &&
         left.details.stage == right.details.stage &&
         left.source_ref.ref_type == right.source_ref.ref_type &&
         left.source_ref.ref_id == right.source_ref.ref_id;
}

// ---------------------------------------------------------------------------
// Layer 1: Required nested evidence validation.
// ---------------------------------------------------------------------------
inline RecoveryRequestGuardResult validate_recovery_request_required_fields(
    const RecoveryRequest& request) {
  if (!request.reflection_decision.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "reflection_decision is required",
    };
  }

  const auto reflection_result =
      validate_reflection_decision_field_rules(*request.reflection_decision);
  if (!reflection_result.ok) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "reflection_decision must pass nested ReflectionDecision validation",
    };
  }

  if (!request.error_info.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "error_info is required",
    };
  }

  const auto error_result = validate_error_info_required_fields(*request.error_info);
  if (!error_result.ok) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "error_info must pass nested ErrorInfo validation",
    };
  }

  if (!request.latest_observation.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "latest_observation is required",
    };
  }

  const auto observation_result =
      validate_observation_boundary(*request.latest_observation);
  if (!observation_result.ok) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "latest_observation must pass nested Observation validation",
    };
  }

  if (!request.checkpoint.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "checkpoint is required",
    };
  }

  const auto checkpoint_result = validate_checkpoint_field_rules(*request.checkpoint);
  if (!checkpoint_result.ok) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "checkpoint must pass nested Checkpoint validation",
    };
  }

  if (!request.idempotency_and_side_effect_report.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "idempotency_and_side_effect_report is required",
    };
  }

  const auto report_result =
      validate_idempotency_and_side_effect_report_required_fields(
          *request.idempotency_and_side_effect_report);
  if (!report_result.ok) {
    return report_result;
  }

  return RecoveryRequestGuardResult{
      .ok = true,
      .reason = "all required recovery-request evidence present",
  };
}

// ---------------------------------------------------------------------------
// Layer 2: Boundary validation.
// ---------------------------------------------------------------------------
inline RecoveryRequestGuardResult validate_recovery_request_boundary(
    const RecoveryRequest& request) {
  const auto required_result =
      validate_recovery_request_required_fields(request);
  if (!required_result.ok) {
    return required_result;
  }

  if (*request.latest_observation->success) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "latest_observation must describe a failed execution",
    };
  }

  if (!request.latest_observation->error.has_value()) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "latest_observation.error must be present for recovery admission",
    };
  }

  if (*request.checkpoint->state == CheckpointState::Succeeded) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "checkpoint.state must not be Succeeded for recovery admission",
    };
  }

  if (request.retry_count.has_value() && request.checkpoint->retry_count.has_value() &&
      *request.retry_count != *request.checkpoint->retry_count) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = "retry_count must match checkpoint.retry_count when both are present",
    };
  }

  if (request.runtime_budget_snapshot.has_value()) {
    const auto budget_result =
        validate_budget_snapshot(*request.runtime_budget_snapshot);
    if (!budget_result.ok) {
      return RecoveryRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget_snapshot must pass nested BudgetSnapshot validation",
      };
    }
  }

  return RecoveryRequestGuardResult{
      .ok = true,
      .reason = "recovery request boundary validation passed",
  };
}

// ---------------------------------------------------------------------------
// Layer 3: Field-level validation.
//
// T010 adds field-table hygiene and cross-object consistency rules on top of
// the T009 required/boundary guards:
//   1. latest_observation must also satisfy source→correlation L3 rules.
//   2. request_id / goal_id must remain aligned across nested evidence.
//   3. top-level error_info must mirror latest_observation.error.
//   4. error_info.source_ref must align with latest_observation source.
//   5. replay_safe=true with observed side-effects requires an idempotency_key.
// ---------------------------------------------------------------------------
inline RecoveryRequestGuardResult validate_recovery_request_field_rules(
    const RecoveryRequest& request) {
  const auto boundary_result = validate_recovery_request_boundary(request);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  const auto observation_correlation_result =
      validate_observation_source_correlation(*request.latest_observation);
  if (!observation_correlation_result.ok) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason = observation_correlation_result.reason,
    };
  }

  if (!recovery_request_optional_string_values_match(
          request.reflection_decision->request_id,
          request.latest_observation->request_id) ||
      !recovery_request_optional_string_values_match(
          request.reflection_decision->request_id,
          request.checkpoint->request_id) ||
      !recovery_request_optional_string_values_match(
          request.latest_observation->request_id,
          request.checkpoint->request_id)) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "request_id values across reflection_decision/latest_observation/checkpoint must match when present",
    };
  }

  if (!recovery_request_optional_string_values_match(
          request.reflection_decision->goal_id,
          request.latest_observation->goal_id) ||
      !recovery_request_optional_string_values_match(
          request.reflection_decision->goal_id,
          request.checkpoint->goal_id) ||
      !recovery_request_optional_string_values_match(
          request.latest_observation->goal_id,
          request.checkpoint->goal_id)) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "goal_id values across reflection_decision/latest_observation/checkpoint must match when present",
    };
  }

  if (!recovery_request_error_info_matches(*request.error_info,
                                           *request.latest_observation->error)) {
    return RecoveryRequestGuardResult{
        .ok = false,
        .reason =
            "error_info must mirror latest_observation.error when latest_observation.error is present",
    };
  }

  switch (*request.latest_observation->source) {
    case ObservationSource::ToolExecution:
      if (request.error_info->source_ref.ref_type != "tool_call") {
        return RecoveryRequestGuardResult{
            .ok = false,
            .reason =
                "error_info.source_ref.ref_type must be tool_call when latest_observation.source is ToolExecution",
        };
      }
      if (!request.latest_observation->tool_call_id.has_value() ||
          request.error_info->source_ref.ref_id !=
              *request.latest_observation->tool_call_id) {
        return RecoveryRequestGuardResult{
            .ok = false,
            .reason =
                "error_info.source_ref.ref_id must match latest_observation.tool_call_id for ToolExecution observations",
        };
      }
      break;

    case ObservationSource::WorkerAgent:
      if (request.error_info->source_ref.ref_type != "worker_task") {
        return RecoveryRequestGuardResult{
            .ok = false,
            .reason =
                "error_info.source_ref.ref_type must be worker_task when latest_observation.source is WorkerAgent",
        };
      }
      if (!request.latest_observation->worker_task_id.has_value() ||
          request.error_info->source_ref.ref_id !=
              *request.latest_observation->worker_task_id) {
        return RecoveryRequestGuardResult{
            .ok = false,
            .reason =
                "error_info.source_ref.ref_id must match latest_observation.worker_task_id for WorkerAgent observations",
        };
      }
      break;

    case ObservationSource::Retrieval:
    case ObservationSource::HumanFeedback:
      if (request.error_info->source_ref.ref_type != "observation") {
        return RecoveryRequestGuardResult{
            .ok = false,
            .reason =
                "error_info.source_ref.ref_type must be observation when latest_observation.source is Retrieval or HumanFeedback",
        };
      }
      if (request.error_info->source_ref.ref_id !=
          *request.latest_observation->observation_id) {
        return RecoveryRequestGuardResult{
            .ok = false,
            .reason =
                "error_info.source_ref.ref_id must match latest_observation.observation_id for Retrieval or HumanFeedback observations",
        };
      }
      break;

    case ObservationSource::Unspecified:
      return RecoveryRequestGuardResult{
          .ok = false,
          .reason =
              "latest_observation.source must be present and not Unspecified",
      };
  }

  const auto report_field_result =
      validate_idempotency_and_side_effect_report_field_rules(
          *request.idempotency_and_side_effect_report);
  if (!report_field_result.ok) {
    return report_field_result;
  }

  return RecoveryRequestGuardResult{
      .ok = true,
      .reason = "recovery request field rules validation passed",
  };
}

// ---------------------------------------------------------------------------
// Field-name boundary wrapper for T009 top-level forbidden fields.
// ---------------------------------------------------------------------------
inline RecoveryRequestBoundaryResult evaluate_recovery_request_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field :
       kRecoveryRequestReflectionTopLevelForbiddenFields) {
    if (field_name == forbidden_field) {
      return RecoveryRequestBoundaryResult{
          .allowed = false,
          .decision =
              RecoveryRequestBoundaryDecision::RejectReflectionTopLevelField,
          .reason =
              "recovery request must not duplicate reflection-decision top-level semantics",
      };
    }
  }

  for (const auto forbidden_field : kRecoveryRequestOutcomeForbiddenFields) {
    if (field_name == forbidden_field) {
      return RecoveryRequestBoundaryResult{
          .allowed = false,
          .decision = RecoveryRequestBoundaryDecision::RejectOutcomeField,
          .reason =
              "recovery request must not contain recovery-outcome result fields",
      };
    }
  }

  for (const auto forbidden_field :
       kRecoveryRequestExecutionSchedulingForbiddenFields) {
    if (field_name == forbidden_field) {
      return RecoveryRequestBoundaryResult{
          .allowed = false,
          .decision =
              RecoveryRequestBoundaryDecision::RejectExecutionSchedulingField,
          .reason =
              "recovery request must not contain execution scheduling outputs",
      };
    }
  }

  return RecoveryRequestBoundaryResult{};
}

inline RecoveryRequestBoundaryResult
validate_recovery_request_contract_field_boundary(std::string_view field_name) {
  return evaluate_recovery_request_field_boundary(field_name);
}

}  // namespace dasall::contracts