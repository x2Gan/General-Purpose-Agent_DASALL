#pragma once

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "task/TaskDomainContracts.h"

namespace dasall::contracts {

// TaskDomainBoundaryDecision normalizes guard outcomes for stable contract-test
// assertions.
enum class TaskDomainBoundaryDecision {
  AllowField,
  RejectGlobalStateField,
  RejectRecoveryEntryField,
  RejectFinalResultField,
};

// TaskDomainGuardResult carries both boolean and semantic decision metadata.
struct TaskDomainGuardResult {
  bool ok = false;
  TaskDomainBoundaryDecision decision = TaskDomainBoundaryDecision::AllowField;
  std::string_view reason = "task domain validation failed";
};

// Frozen top-level global-state aliases that task-domain objects must reject.
inline constexpr std::array<std::string_view, 4>
    kTaskDomainGlobalStateForbiddenFields = {
        "session_id",
        "global_session_state",
        "global_fsm_state",
        "session_fsm_state",
};

// Frozen top-level recovery-entry aliases that task-domain objects must reject.
inline constexpr std::array<std::string_view, 2>
    kTaskDomainRecoveryForbiddenFields = {
        "checkpoint_ref",
        "resume_token",
};

// Frozen top-level final-result aliases that task-domain objects must reject.
inline constexpr std::array<std::string_view, 3>
    kTaskDomainResultForbiddenFields = {
        "agent_result",
        "final_agent_response",
        "merged_result",
};

// Returns true when the candidate string has at least one non-whitespace
// character.
inline bool task_domain_string_has_non_whitespace_content(std::string_view value) {
  for (const unsigned char ch : value) {
    if (!std::isspace(ch)) {
      return true;
    }
  }

  return false;
}

// Validates the required fields of SubTaskGraph.
// Required fields are frozen as graph_id/root_task_id/task_ids.
inline TaskDomainGuardResult validate_subtask_graph_required_fields(
    const SubTaskGraph& graph) {
  if (!has_non_empty_value(graph.graph_id)) {
    return TaskDomainGuardResult{
        .ok = false,
        .decision = TaskDomainBoundaryDecision::AllowField,
        .reason = "graph_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(graph.root_task_id)) {
    return TaskDomainGuardResult{
        .ok = false,
        .decision = TaskDomainBoundaryDecision::AllowField,
        .reason = "root_task_id is required and must be non-empty",
    };
  }

  if (!graph.task_ids.has_value() || graph.task_ids->empty()) {
    return TaskDomainGuardResult{
        .ok = false,
        .decision = TaskDomainBoundaryDecision::AllowField,
        .reason = "task_ids are required and must contain at least one item",
    };
  }

  return TaskDomainGuardResult{
      .ok = true,
      .decision = TaskDomainBoundaryDecision::AllowField,
      .reason = "all required subtask graph fields present",
  };
}

// Validates SubTaskGraph field-level hygiene on top of required checks.
inline TaskDomainGuardResult validate_subtask_graph_field_rules(
    const SubTaskGraph& graph) {
  const auto required_result = validate_subtask_graph_required_fields(graph);
  if (!required_result.ok) {
    return required_result;
  }

  if (!task_domain_string_has_non_whitespace_content(*graph.graph_id)) {
    return TaskDomainGuardResult{
        .ok = false,
        .decision = TaskDomainBoundaryDecision::AllowField,
        .reason = "graph_id must contain at least one non-whitespace character",
    };
  }

  if (!task_domain_string_has_non_whitespace_content(*graph.root_task_id)) {
    return TaskDomainGuardResult{
        .ok = false,
        .decision = TaskDomainBoundaryDecision::AllowField,
        .reason =
            "root_task_id must contain at least one non-whitespace character",
    };
  }

  for (const auto& task_id : *graph.task_ids) {
    if (!task_domain_string_has_non_whitespace_content(task_id)) {
      return TaskDomainGuardResult{
          .ok = false,
          .decision = TaskDomainBoundaryDecision::AllowField,
          .reason =
              "task_ids must not contain empty or whitespace-only items",
      };
    }
  }

  if (graph.graph_revision.has_value() && *graph.graph_revision == 0U) {
    return TaskDomainGuardResult{
        .ok = false,
        .decision = TaskDomainBoundaryDecision::AllowField,
        .reason = "graph_revision must be greater than zero when present",
    };
  }

  return TaskDomainGuardResult{
      .ok = true,
      .decision = TaskDomainBoundaryDecision::AllowField,
      .reason = "subtask graph field rules validation passed",
  };
}

// Evaluates a candidate top-level field alias against task-domain forbidden
// boundaries.
constexpr TaskDomainGuardResult evaluate_task_domain_forbidden_field(
    std::string_view field_name) {
  for (const auto forbidden_field : kTaskDomainGlobalStateForbiddenFields) {
    if (field_name == forbidden_field) {
      return TaskDomainGuardResult{
          .ok = false,
          .decision = TaskDomainBoundaryDecision::RejectGlobalStateField,
          .reason =
              "task domain objects must not carry top-level session or fsm state",
      };
    }
  }

  for (const auto forbidden_field : kTaskDomainRecoveryForbiddenFields) {
    if (field_name == forbidden_field) {
      return TaskDomainGuardResult{
          .ok = false,
          .decision = TaskDomainBoundaryDecision::RejectRecoveryEntryField,
          .reason =
              "task domain objects must not become checkpoint or resume entry",
      };
    }
  }

  for (const auto forbidden_field : kTaskDomainResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return TaskDomainGuardResult{
          .ok = false,
          .decision = TaskDomainBoundaryDecision::RejectFinalResultField,
          .reason =
              "task domain objects must not carry top-level final-result semantics",
      };
    }
  }

  return TaskDomainGuardResult{
      .ok = true,
      .decision = TaskDomainBoundaryDecision::AllowField,
      .reason = "task domain field is allowed by WP05-T008 boundary",
  };
}

// Boolean helper for callers that only need allow/reject outcome.
constexpr bool is_allowed_task_domain_field(std::string_view field_name) {
  return evaluate_task_domain_forbidden_field(field_name).ok;
}

}  // namespace dasall::contracts
