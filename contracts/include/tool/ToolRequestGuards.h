#pragma once

#include <array>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "tool/ToolRequest.h"

namespace dasall::contracts {

enum class ToolRequestBoundaryDecision {
  AllowField,
  RejectExecutionResultField,
  RejectBudgetStateField,
  RejectPromptProviderField,
  RejectDescriptorOwnershipField,
};

struct ToolRequestFieldBoundaryResult {
  bool allowed = true;
  ToolRequestBoundaryDecision decision = ToolRequestBoundaryDecision::AllowField;
  std::string_view reason = "tool request field is allowed by WP05-T002";
};

struct ToolRequestGuardResult {
  bool ok = false;
  std::string_view reason = "tool request validation failed";
};

inline constexpr std::array<std::string_view, 6>
    kToolRequestExecutionResultForbiddenFields = {
        "error",
        "error_info",
        "result_payload",
        "observation",
        "observation_digest",
        "side_effects",
};

inline constexpr std::array<std::string_view, 4>
    kToolRequestBudgetStateForbiddenFields = {
        "budget_snapshot",
        "remaining_budget",
        "spent_tokens",
        "current_budget_usage",
};

inline constexpr std::array<std::string_view, 3>
    kToolRequestPromptProviderForbiddenFields = {
        "rendered_prompt",
        "provider_payload",
        "final_messages",
};

inline constexpr std::array<std::string_view, 3>
    kToolRequestDescriptorOwnershipForbiddenFields = {
        "tool_schema",
        "tool_descriptor",
        "tool_ir",
};

inline ToolRequestGuardResult validate_tool_request_required_fields(
    const ToolRequest& request) {
  if (!has_non_empty_value(request.request_id)) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.tool_call_id)) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "tool_call_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.tool_name)) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "tool_name is required and must be non-empty",
    };
  }

  if (!request.invocation_kind.has_value() ||
      *request.invocation_kind == ToolInvocationKind::Unspecified) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "invocation_kind is required and must not be Unspecified",
    };
  }

  if (!has_non_empty_value(request.arguments_payload)) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "arguments_payload is required and must be non-empty",
    };
  }

  if (!request.created_at.has_value() || *request.created_at <= 0) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return ToolRequestGuardResult{
      .ok = true,
      .reason = "all required tool request fields present",
  };
}

inline ToolRequestGuardResult validate_tool_request_boundary(
    const ToolRequest& request) {
  const auto required_result = validate_tool_request_required_fields(request);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_kind = static_cast<int>(*request.invocation_kind);
  if (raw_kind < static_cast<int>(ToolInvocationKind::InformationQuery) ||
      raw_kind > static_cast<int>(ToolInvocationKind::Diagnostic)) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "invocation_kind value is outside the known enum range",
    };
  }

  if (*request.tool_call_id == *request.request_id) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "tool_call_id must not equal request_id because call identity and request identity are layered separately",
    };
  }

  return ToolRequestGuardResult{
      .ok = true,
      .reason = "tool request boundary validation passed",
  };
}

inline ToolRequestGuardResult validate_tool_request_field_rules(
    const ToolRequest& request) {
  const auto boundary_result = validate_tool_request_boundary(request);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (request.goal_id.has_value() && request.goal_id->empty()) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "goal_id must be non-empty when present",
    };
  }

  if (request.worker_task_id.has_value() && request.worker_task_id->empty()) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "worker_task_id must be non-empty when present",
    };
  }

  if (request.idempotency_key.has_value() && request.idempotency_key->empty()) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "idempotency_key must be non-empty when present",
    };
  }

  if (request.timeout_ms.has_value() && *request.timeout_ms == 0U) {
    return ToolRequestGuardResult{
        .ok = false,
        .reason = "timeout_ms must be positive when present",
    };
  }

  if (request.runtime_budget.has_value()) {
    const auto& budget = *request.runtime_budget;
    if (budget.max_tokens.has_value() && *budget.max_tokens == 0U) {
      return ToolRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_tokens must be positive when present",
      };
    }
    if (budget.max_turns.has_value() && *budget.max_turns == 0U) {
      return ToolRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_turns must be positive when present",
      };
    }
    if (budget.max_tool_calls.has_value() && *budget.max_tool_calls == 0U) {
      return ToolRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_tool_calls must be positive when present",
      };
    }
    if (budget.max_latency_ms.has_value() && *budget.max_latency_ms == 0U) {
      return ToolRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_latency_ms must be positive when present",
      };
    }
    if (budget.max_replan_count.has_value() &&
        *budget.max_replan_count == 0U) {
      return ToolRequestGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_replan_count must be positive when present",
      };
    }
  }

  if (request.tags.has_value()) {
    if (request.tags->empty()) {
      return ToolRequestGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < request.tags->size(); ++index) {
      if ((*request.tags)[index].empty()) {
        return ToolRequestGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < request.tags->size(); ++probe) {
        if ((*request.tags)[index] == (*request.tags)[probe]) {
          return ToolRequestGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return ToolRequestGuardResult{
      .ok = true,
      .reason = "tool request field rules validation passed",
  };
}

constexpr ToolRequestFieldBoundaryResult evaluate_tool_request_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kToolRequestExecutionResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolRequestFieldBoundaryResult{
          .allowed = false,
          .decision = ToolRequestBoundaryDecision::RejectExecutionResultField,
          .reason = "tool request must not contain execution-result or observation fields",
      };
    }
  }

  for (const auto forbidden_field : kToolRequestBudgetStateForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolRequestFieldBoundaryResult{
          .allowed = false,
          .decision = ToolRequestBoundaryDecision::RejectBudgetStateField,
          .reason = "tool request must not duplicate budget snapshot or usage fields",
      };
    }
  }

  for (const auto forbidden_field : kToolRequestPromptProviderForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolRequestFieldBoundaryResult{
          .allowed = false,
          .decision = ToolRequestBoundaryDecision::RejectPromptProviderField,
          .reason = "tool request must not contain prompt-rendering or provider payload fields",
      };
    }
  }

  for (const auto forbidden_field : kToolRequestDescriptorOwnershipForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolRequestFieldBoundaryResult{
          .allowed = false,
          .decision = ToolRequestBoundaryDecision::RejectDescriptorOwnershipField,
          .reason = "tool request must not own ToolDescriptor or ToolIR fields",
      };
    }
  }

  return ToolRequestFieldBoundaryResult{};
}

inline ToolRequestGuardResult validate_tool_request_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_tool_request_field_boundary(field_name);
  return ToolRequestGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts