#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "task/WorkerLease.h"

namespace dasall::contracts {

struct WorkerLeaseGuardResult {
  bool ok = false;
  std::string_view reason = "worker lease validation failed";
};

inline constexpr std::array<std::string_view, 4>
    kWorkerLeaseGlobalStateForbiddenFields = {
        "global_session_state",
        "global_fsm_state",
        "session_fsm_state",
        "session_id",
};

inline constexpr std::array<std::string_view, 2>
    kWorkerLeaseRecoveryForbiddenFields = {
        "checkpoint_ref",
        "resume_token",
};

inline constexpr std::array<std::string_view, 3>
    kWorkerLeaseResultForbiddenFields = {
        "agent_result",
        "final_agent_response",
        "merged_result",
};

// Validates the required-field rules frozen by WP04-T020:
//   1) lease_id and worker_ref must be present and non-empty.
//   2) deadline_at must be present and a positive timestamp.
inline WorkerLeaseGuardResult validate_worker_lease_required_fields(
    const WorkerLease& lease) {
  if (!has_non_empty_value(lease.lease_id)) {
    return WorkerLeaseGuardResult{
        .ok = false,
        .reason = "lease_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(lease.worker_ref)) {
    return WorkerLeaseGuardResult{
        .ok = false,
        .reason = "worker_ref is required and must be non-empty",
    };
  }

  if (!lease.deadline_at.has_value() || *lease.deadline_at <= 0) {
    return WorkerLeaseGuardResult{
        .ok = false,
        .reason = "deadline_at is required and must be a positive timestamp",
    };
  }

  return WorkerLeaseGuardResult{
      .ok = true,
      .reason = "all required worker lease fields present",
  };
}

// Validates WP04-T020 semantic boundary rules on top of required-field checks:
//   1) renewal_deadline_at, if present, must be positive.
//   2) renewal_deadline_at, if present, must not be later than deadline_at.
//   3) release_reason, if present, must be non-empty.
inline WorkerLeaseGuardResult validate_worker_lease_boundary(
    const WorkerLease& lease) {
  const auto required_result = validate_worker_lease_required_fields(lease);
  if (!required_result.ok) {
    return required_result;
  }

  if (lease.renewal_deadline_at.has_value()) {
    if (*lease.renewal_deadline_at <= 0) {
      return WorkerLeaseGuardResult{
          .ok = false,
          .reason =
              "renewal_deadline_at must be a positive timestamp when present",
      };
    }

    if (*lease.renewal_deadline_at > *lease.deadline_at) {
      return WorkerLeaseGuardResult{
          .ok = false,
          .reason =
              "renewal_deadline_at must not be later than deadline_at",
      };
    }
  }

  if (lease.release_reason.has_value() && lease.release_reason->empty()) {
    return WorkerLeaseGuardResult{
        .ok = false,
        .reason = "release_reason must be non-empty when present",
    };
  }

  return WorkerLeaseGuardResult{
      .ok = true,
      .reason = "worker lease boundary validation passed",
  };
}

// Validates a candidate top-level field name against the T020 WorkerLease
// semantic boundary.
inline WorkerLeaseGuardResult validate_worker_lease_forbidden_field(
    std::string_view field_name) {
  for (const auto forbidden_field : kWorkerLeaseGlobalStateForbiddenFields) {
    if (field_name == forbidden_field) {
      return WorkerLeaseGuardResult{
          .ok = false,
          .reason =
              "worker lease must not carry global session or fsm state",
      };
    }
  }

  for (const auto forbidden_field : kWorkerLeaseRecoveryForbiddenFields) {
    if (field_name == forbidden_field) {
      return WorkerLeaseGuardResult{
          .ok = false,
          .reason =
              "worker lease must not become a checkpoint or resume entry",
      };
    }
  }

  for (const auto forbidden_field : kWorkerLeaseResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return WorkerLeaseGuardResult{
          .ok = false,
          .reason =
              "worker lease must not carry final-result or merged-result semantics",
      };
    }
  }

  return WorkerLeaseGuardResult{
      .ok = true,
      .reason = "worker lease field is allowed by WP04-T020 boundary",
  };
}

}  // namespace dasall::contracts