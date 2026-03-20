#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "tool/ToolResult.h"

namespace dasall::contracts {

enum class ToolResultBoundaryDecision {
  AllowField,
  RejectObservationOwnershipField,
  RejectRuntimeAccountingField,
  RejectPromptProviderField,
  RejectDescriptorOwnershipField,
  RejectRecoveryControlField,
};

struct ToolResultFieldBoundaryResult {
  bool allowed = true;
  ToolResultBoundaryDecision decision = ToolResultBoundaryDecision::AllowField;
  std::string_view reason = "tool result field is allowed by WP05-T003";
};

struct ToolResultGuardResult {
  bool ok = false;
  std::string_view reason = "tool result validation failed";
};

inline constexpr std::array<std::string_view, 5>
    kToolResultObservationOwnershipForbiddenFields = {
        "observation",
        "observation_id",
        "observation_digest",
        "source",
        "summary",
};

inline constexpr std::array<std::string_view, 5>
    kToolResultRuntimeAccountingForbiddenFields = {
        "budget_snapshot",
        "remaining_budget",
        "spent_tokens",
        "retry_count",
        "backoff_ms",
};

inline constexpr std::array<std::string_view, 3>
    kToolResultPromptProviderForbiddenFields = {
        "rendered_prompt",
        "provider_payload",
        "final_messages",
};

inline constexpr std::array<std::string_view, 3>
    kToolResultDescriptorOwnershipForbiddenFields = {
        "tool_schema",
        "tool_descriptor",
        "tool_ir",
};

inline constexpr std::array<std::string_view, 4>
    kToolResultRecoveryControlForbiddenFields = {
        "compensation_plan",
        "compensation_result",
        "checkpoint_ref",
        "recovery_action",
};

inline ToolResultGuardResult validate_tool_result_required_fields(
    const ToolResult& result) {
  if (!has_non_empty_value(result.request_id)) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(result.tool_call_id)) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "tool_call_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(result.tool_name)) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "tool_name is required and must be non-empty",
    };
  }

  if (!result.success.has_value()) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "success is required",
    };
  }

  if (!result.completed_at.has_value() || *result.completed_at <= 0) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "completed_at is required and must be a positive timestamp",
    };
  }

  return ToolResultGuardResult{
      .ok = true,
      .reason = "all required tool result fields present",
  };
}

inline ToolResultGuardResult validate_tool_result_boundary(
    const ToolResult& result) {
  const auto required_result = validate_tool_result_required_fields(result);
  if (!required_result.ok) {
    return required_result;
  }

  if (*result.tool_call_id == *result.request_id) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "tool_call_id must not equal request_id because execution identity and request identity are layered separately",
    };
  }

  if (result.duration_ms.has_value() && *result.duration_ms <= 0) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "duration_ms must be positive when present",
    };
  }

  if (*result.success) {
    if (!has_non_empty_value(result.payload)) {
      return ToolResultGuardResult{
          .ok = false,
          .reason = "payload is required and must be non-empty when success is true",
      };
    }
    if (result.error.has_value()) {
      return ToolResultGuardResult{
          .ok = false,
          .reason = "error must not be present when success is true",
      };
    }
  } else if (!result.error.has_value()) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "error must be present when success is false",
    };
  }

  return ToolResultGuardResult{
      .ok = true,
      .reason = "tool result boundary validation passed",
  };
}

inline ToolResultGuardResult validate_tool_result_field_rules(
    const ToolResult& result) {
  const auto boundary_result = validate_tool_result_boundary(result);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (result.payload.has_value() && result.payload->empty()) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "payload must be non-empty when present",
    };
  }

  if (result.goal_id.has_value() && result.goal_id->empty()) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  if (result.worker_task_id.has_value() && result.worker_task_id->empty()) {
    return ToolResultGuardResult{
        .ok = false,
        .reason = "worker_task_id must be non-empty when present",
    };
  }

  if (result.side_effects.has_value()) {
    if (result.side_effects->empty()) {
      return ToolResultGuardResult{
          .ok = false,
          .reason = "side_effects must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < result.side_effects->size(); ++index) {
      if ((*result.side_effects)[index].empty()) {
        return ToolResultGuardResult{
            .ok = false,
            .reason = "side_effects must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1;
           probe < result.side_effects->size();
           ++probe) {
        if ((*result.side_effects)[index] == (*result.side_effects)[probe]) {
          return ToolResultGuardResult{
              .ok = false,
              .reason = "side_effects must not contain duplicate items",
          };
        }
      }
    }
  }

  if (result.tags.has_value()) {
    if (result.tags->empty()) {
      return ToolResultGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < result.tags->size(); ++index) {
      if ((*result.tags)[index].empty()) {
        return ToolResultGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < result.tags->size(); ++probe) {
        if ((*result.tags)[index] == (*result.tags)[probe]) {
          return ToolResultGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return ToolResultGuardResult{
      .ok = true,
      .reason = "tool result field rules validation passed",
  };
}

constexpr ToolResultFieldBoundaryResult evaluate_tool_result_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kToolResultObservationOwnershipForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolResultFieldBoundaryResult{
          .allowed = false,
          .decision = ToolResultBoundaryDecision::RejectObservationOwnershipField,
          .reason = "tool result must not own Observation or ObservationDigest fields",
      };
    }
  }

  for (const auto forbidden_field : kToolResultRuntimeAccountingForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolResultFieldBoundaryResult{
          .allowed = false,
          .decision = ToolResultBoundaryDecision::RejectRuntimeAccountingField,
          .reason = "tool result must not duplicate runtime accounting or budget snapshot fields",
      };
    }
  }

  for (const auto forbidden_field : kToolResultPromptProviderForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolResultFieldBoundaryResult{
          .allowed = false,
          .decision = ToolResultBoundaryDecision::RejectPromptProviderField,
          .reason = "tool result must not contain prompt-rendering or provider payload fields",
      };
    }
  }

  for (const auto forbidden_field : kToolResultDescriptorOwnershipForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolResultFieldBoundaryResult{
          .allowed = false,
          .decision = ToolResultBoundaryDecision::RejectDescriptorOwnershipField,
          .reason = "tool result must not own ToolDescriptor or ToolIR fields",
      };
    }
  }

  for (const auto forbidden_field : kToolResultRecoveryControlForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolResultFieldBoundaryResult{
          .allowed = false,
          .decision = ToolResultBoundaryDecision::RejectRecoveryControlField,
          .reason = "tool result must not carry recovery or compensation control-plan fields",
      };
    }
  }

  return ToolResultFieldBoundaryResult{};
}

inline ToolResultGuardResult validate_tool_result_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_tool_result_field_boundary(field_name);
  return ToolResultGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts