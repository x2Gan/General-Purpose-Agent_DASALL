#pragma once

#include <string_view>

#include "boundary/GuardCommon.h"
#include "boundary/MultiAgentBoundaryGuards.h"
#include "task/WorkerTask.h"

namespace dasall::contracts {

struct WorkerTaskGuardResult {
  bool ok = false;
  std::string_view reason = "worker task validation failed";
};

// Validates the required-field rules frozen by WP04-T018:
//   1) task_id, parent_task_id, lease_id, worker_type must be present and
//      non-empty.
//   2) allowed_tools must be present and contain at least one item.
//   3) timeout_ms must be present and greater than zero.
inline WorkerTaskGuardResult validate_worker_task_required_fields(
    const WorkerTask& task) {
  if (!has_non_empty_value(task.task_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "task_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(task.parent_task_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "parent_task_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(task.lease_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "lease_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(task.worker_type)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "worker_type is required and must be non-empty",
    };
  }

  if (!task.allowed_tools.has_value() || task.allowed_tools->empty()) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "allowed_tools are required and must contain at least one item",
    };
  }

  if (!task.timeout_ms.has_value() || *task.timeout_ms == 0U) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "timeout_ms is required and must be greater than zero",
    };
  }

  return WorkerTaskGuardResult{
      .ok = true,
      .reason = "all required worker task fields present",
  };
}

// Validates WP04-T018 semantic boundary rules on top of required-field checks:
//   1) task_id and parent_task_id must remain layered and not collapse.
//   2) allowed_tools must not contain empty strings.
//   3) idempotency_key, if present, must be non-empty.
inline WorkerTaskGuardResult validate_worker_task_boundary(
    const WorkerTask& task) {
  const auto required_result = validate_worker_task_required_fields(task);
  if (!required_result.ok) {
    return required_result;
  }

  if (task.task_id == task.parent_task_id) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason =
            "task_id must not equal parent_task_id because worker and parent anchors are layered separately",
    };
  }

  for (const auto& allowed_tool : *task.allowed_tools) {
    if (allowed_tool.empty()) {
      return WorkerTaskGuardResult{
          .ok = false,
          .reason = "allowed_tools must not contain empty strings",
      };
    }
  }

  if (task.idempotency_key.has_value() && task.idempotency_key->empty()) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "idempotency_key must be non-empty when present",
    };
  }

  return WorkerTaskGuardResult{
      .ok = true,
      .reason = "worker task boundary validation passed",
  };
}

// Validates a candidate top-level field name against the shared ADR-008 worker
// task boundary guard.
inline WorkerTaskGuardResult validate_worker_task_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_worker_task_field_boundary(field_name);
  return WorkerTaskGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts