#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

// IdentityMetadata freezes the WP02-T009 cross-cutting identity surface.
// All five top-level IDs are required and parent_task_id follows propagation
// semantics: absent for root tasks, required for child tasks.
struct IdentityMetadata {
  std::optional<std::string> request_id;
  std::optional<std::string> session_id;
  std::optional<std::string> trace_id;
  std::optional<std::string> task_id;
  std::optional<std::string> lease_id;
  std::optional<std::string> parent_task_id;

  // True means the current task is created from another parent task context
  // and must carry a valid parent_task_id reference.
  bool is_child_task = false;
};

struct IdentityMetadataGuardResult {
  bool ok = false;
  std::string_view reason = "identity metadata validation failed";
};

// Validates WP02-T009 + T014 D5 rules:
// 1) request/session/trace/task/lease IDs are all required and non-empty.
// 2) parent_task_id is required for child tasks and must not self-reference.
// 3) root tasks must not set parent_task_id.
inline IdentityMetadataGuardResult validate_identity_metadata(const IdentityMetadata& metadata) {
  if (!has_non_empty_value(metadata.request_id)) {
    return IdentityMetadataGuardResult{.ok = false, .reason = "request_id is required"};
  }

  if (!has_non_empty_value(metadata.session_id)) {
    return IdentityMetadataGuardResult{.ok = false, .reason = "session_id is required"};
  }

  if (!has_non_empty_value(metadata.trace_id)) {
    return IdentityMetadataGuardResult{.ok = false, .reason = "trace_id is required"};
  }

  if (!has_non_empty_value(metadata.task_id)) {
    return IdentityMetadataGuardResult{.ok = false, .reason = "task_id is required"};
  }

  if (!has_non_empty_value(metadata.lease_id)) {
    return IdentityMetadataGuardResult{.ok = false, .reason = "lease_id is required"};
  }

  const bool has_parent_task_id = has_non_empty_value(metadata.parent_task_id);
  if (metadata.is_child_task && !has_parent_task_id) {
    return IdentityMetadataGuardResult{.ok = false,
                                       .reason = "parent_task_id is required for child task"};
  }

  if (!metadata.is_child_task && has_parent_task_id) {
    return IdentityMetadataGuardResult{.ok = false,
                                       .reason = "parent_task_id must be empty for root task"};
  }

  if (has_parent_task_id && metadata.parent_task_id == metadata.task_id) {
    return IdentityMetadataGuardResult{.ok = false,
                                       .reason = "parent_task_id must not equal task_id"};
  }

  return IdentityMetadataGuardResult{.ok = true, .reason = "identity metadata is valid"};
}

}  // namespace dasall::contracts
