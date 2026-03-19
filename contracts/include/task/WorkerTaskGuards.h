#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"
#include "boundary/MultiAgentBoundaryGuards.h"
#include "task/WorkerTask.h"

namespace dasall::contracts {

struct WorkerTaskGuardResult {
  bool ok = false;
  std::string_view reason = "worker task validation failed";
};

inline bool worker_task_string_has_non_whitespace_content(
    std::string_view value) {
  for (const unsigned char ch : value) {
    if (!std::isspace(ch)) {
      return true;
    }
  }

  return false;
}

inline bool worker_task_optional_string_has_non_whitespace_content(
    const std::optional<std::string>& value) {
  return !value.has_value() ||
         worker_task_string_has_non_whitespace_content(*value);
}

inline std::string_view worker_task_trimmed_view(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }

  return value.substr(begin, end - begin);
}

inline bool worker_task_has_duplicate_allowed_tools(
    const std::vector<std::string>& allowed_tools) {
  for (std::size_t index = 0; index < allowed_tools.size(); ++index) {
    const auto current = worker_task_trimmed_view(allowed_tools[index]);
    for (std::size_t probe = index + 1; probe < allowed_tools.size(); ++probe) {
      if (current == worker_task_trimmed_view(allowed_tools[probe])) {
        return true;
      }
    }
  }

  return false;
}

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

// Validates WP04-T019 field-table rules on top of the T018 required/boundary
// guards:
//   1) required string fields must contain non-whitespace content.
//   2) allowed_tools items must be non-whitespace and unique after trimming.
//   3) idempotency_key, if present, must contain non-whitespace content.
//   4) task_id and parent_task_id must remain distinct after trimming.
inline WorkerTaskGuardResult validate_worker_task_field_rules(
    const WorkerTask& task) {
  const auto boundary_result = validate_worker_task_boundary(task);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (!worker_task_string_has_non_whitespace_content(*task.task_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "task_id must contain at least one non-whitespace character",
    };
  }

  if (!worker_task_string_has_non_whitespace_content(*task.parent_task_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason =
            "parent_task_id must contain at least one non-whitespace character",
    };
  }

  if (!worker_task_string_has_non_whitespace_content(*task.lease_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "lease_id must contain at least one non-whitespace character",
    };
  }

  if (!worker_task_string_has_non_whitespace_content(*task.worker_type)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "worker_type must contain at least one non-whitespace character",
    };
  }

  if (worker_task_trimmed_view(*task.task_id) ==
      worker_task_trimmed_view(*task.parent_task_id)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason =
            "task_id and parent_task_id must remain distinct after trimming whitespace",
    };
  }

  for (const auto& allowed_tool : *task.allowed_tools) {
    if (!worker_task_string_has_non_whitespace_content(allowed_tool)) {
      return WorkerTaskGuardResult{
          .ok = false,
          .reason =
              "allowed_tools must not contain empty or whitespace-only items",
      };
    }
  }

  if (worker_task_has_duplicate_allowed_tools(*task.allowed_tools)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason = "allowed_tools must not contain duplicate items",
    };
  }

  if (!worker_task_optional_string_has_non_whitespace_content(
          task.idempotency_key)) {
    return WorkerTaskGuardResult{
        .ok = false,
        .reason =
            "idempotency_key must contain at least one non-whitespace character when present",
    };
  }

  return WorkerTaskGuardResult{
      .ok = true,
      .reason = "worker task field rules validation passed",
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