#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "boundary/GuardCommon.h"

namespace dasall::contracts {

enum class ToolCategory {
  Unspecified = 0,
  Information = 1,
  Action = 2,
  Workflow = 3,
  AgentDelegation = 4,
  Diagnostic = 5,
};

enum class ToolCapabilityTier {
  Unspecified = 0,
  Stable = 1,
  Preview = 2,
  Internal = 3,
};

enum class ToolDescriptorBoundaryDecision {
  AllowField,
  RejectInvocationRuntimeField,
  RejectExecutionResultField,
  RejectRuntimeAccountingField,
};

struct ToolDescriptorFieldBoundaryResult {
  bool allowed = true;
  ToolDescriptorBoundaryDecision decision = ToolDescriptorBoundaryDecision::AllowField;
  std::string_view reason = "tool descriptor field is allowed by WP05-T004";
};

struct ToolDescriptorGuardResult {
  bool ok = false;
  std::string_view reason = "tool descriptor validation failed";
};

struct ToolDescriptor {
  // Stable registry key consumed by ToolRegistry and list_tools surfaces.
  std::optional<std::string> tool_name;

  // Human-friendly name for capability catalogs and diagnostics.
  std::optional<std::string> display_name;

  // Stable tool class for policy and routing strategies.
  std::optional<ToolCategory> category;

  // Release-state signal for rollout and admission gates.
  std::optional<ToolCapabilityTier> capability_tier;

  // Whether the tool is side-effect free by contract.
  std::optional<bool> is_read_only;

  // Whether side effects have a supported compensation strategy.
  std::optional<bool> supports_compensation;

  // Default timeout for invocations when request override is absent.
  std::optional<std::uint32_t> default_timeout_ms;

  // Stable schema anchors for request/response payload governance.
  std::optional<std::string> input_schema_ref;
  std::optional<std::string> output_schema_ref;

  // Policy gate scopes required before execution is admitted.
  std::optional<std::vector<std::string>> required_scopes;

  // Descriptor metadata tags for cataloging and routing hints.
  std::optional<std::vector<std::string>> tags;

  // Descriptor version string for compatibility tracking.
  std::optional<std::string> version;
};

inline constexpr std::array<std::string_view, 6>
    kToolDescriptorInvocationRuntimeForbiddenFields = {
        "request_id",
        "tool_call_id",
        "arguments_payload",
        "normalized_arguments",
        "idempotency_key",
        "timeout_ms",
};

inline constexpr std::array<std::string_view, 4>
    kToolDescriptorExecutionResultForbiddenFields = {
        "payload",
        "error",
        "side_effects",
        "observation",
};

inline constexpr std::array<std::string_view, 4>
    kToolDescriptorRuntimeAccountingForbiddenFields = {
        "budget_snapshot",
        "remaining_budget",
        "spent_tokens",
        "duration_ms",
};

inline ToolDescriptorGuardResult validate_tool_descriptor_required_fields(
    const ToolDescriptor& descriptor) {
  if (!has_non_empty_value(descriptor.tool_name)) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "tool_name is required and must be non-empty",
    };
  }

  if (!descriptor.category.has_value() ||
      *descriptor.category == ToolCategory::Unspecified) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "category is required and must not be Unspecified",
    };
  }

  return ToolDescriptorGuardResult{
      .ok = true,
      .reason = "all required tool descriptor fields present",
  };
}

inline ToolDescriptorGuardResult validate_tool_descriptor_field_rules(
    const ToolDescriptor& descriptor) {
  const auto required_result = validate_tool_descriptor_required_fields(descriptor);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_category = static_cast<int>(*descriptor.category);
  if (raw_category < static_cast<int>(ToolCategory::Information) ||
      raw_category > static_cast<int>(ToolCategory::Diagnostic)) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "category value is outside the known enum range",
    };
  }

  if (descriptor.capability_tier.has_value()) {
    const int raw_tier = static_cast<int>(*descriptor.capability_tier);
    if (raw_tier < static_cast<int>(ToolCapabilityTier::Stable) ||
        raw_tier > static_cast<int>(ToolCapabilityTier::Internal)) {
      return ToolDescriptorGuardResult{
          .ok = false,
          .reason = "capability_tier value is outside the known enum range",
      };
    }
  }

  if (descriptor.display_name.has_value() && descriptor.display_name->empty()) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "display_name must be non-empty when present",
    };
  }

  if (descriptor.default_timeout_ms.has_value() &&
      *descriptor.default_timeout_ms == 0U) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "default_timeout_ms must be positive when present",
    };
  }

  if (descriptor.input_schema_ref.has_value() &&
      descriptor.input_schema_ref->empty()) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "input_schema_ref must be non-empty when present",
    };
  }

  if (descriptor.output_schema_ref.has_value() &&
      descriptor.output_schema_ref->empty()) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "output_schema_ref must be non-empty when present",
    };
  }

  if (descriptor.version.has_value() && descriptor.version->empty()) {
    return ToolDescriptorGuardResult{
        .ok = false,
        .reason = "version must be non-empty when present",
    };
  }

  if (descriptor.required_scopes.has_value()) {
    if (descriptor.required_scopes->empty()) {
      return ToolDescriptorGuardResult{
          .ok = false,
          .reason = "required_scopes must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < descriptor.required_scopes->size(); ++index) {
      if ((*descriptor.required_scopes)[index].empty()) {
        return ToolDescriptorGuardResult{
            .ok = false,
            .reason = "required_scopes must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1;
           probe < descriptor.required_scopes->size();
           ++probe) {
        if ((*descriptor.required_scopes)[index] ==
            (*descriptor.required_scopes)[probe]) {
          return ToolDescriptorGuardResult{
              .ok = false,
              .reason = "required_scopes must not contain duplicate items",
          };
        }
      }
    }
  }

  if (descriptor.tags.has_value()) {
    if (descriptor.tags->empty()) {
      return ToolDescriptorGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < descriptor.tags->size(); ++index) {
      if ((*descriptor.tags)[index].empty()) {
        return ToolDescriptorGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < descriptor.tags->size(); ++probe) {
        if ((*descriptor.tags)[index] == (*descriptor.tags)[probe]) {
          return ToolDescriptorGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return ToolDescriptorGuardResult{
      .ok = true,
      .reason = "tool descriptor field rules validation passed",
  };
}

constexpr ToolDescriptorFieldBoundaryResult evaluate_tool_descriptor_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kToolDescriptorInvocationRuntimeForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolDescriptorFieldBoundaryResult{
          .allowed = false,
          .decision = ToolDescriptorBoundaryDecision::RejectInvocationRuntimeField,
          .reason = "tool descriptor must not contain invocation runtime fields",
      };
    }
  }

  for (const auto forbidden_field : kToolDescriptorExecutionResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolDescriptorFieldBoundaryResult{
          .allowed = false,
          .decision = ToolDescriptorBoundaryDecision::RejectExecutionResultField,
          .reason = "tool descriptor must not contain execution result fields",
      };
    }
  }

  for (const auto forbidden_field : kToolDescriptorRuntimeAccountingForbiddenFields) {
    if (field_name == forbidden_field) {
      return ToolDescriptorFieldBoundaryResult{
          .allowed = false,
          .decision = ToolDescriptorBoundaryDecision::RejectRuntimeAccountingField,
          .reason = "tool descriptor must not contain runtime accounting fields",
      };
    }
  }

  return ToolDescriptorFieldBoundaryResult{};
}

inline ToolDescriptorGuardResult validate_tool_descriptor_forbidden_field(
    std::string_view field_name) {
  const auto result = evaluate_tool_descriptor_field_boundary(field_name);
  return ToolDescriptorGuardResult{
      .ok = result.allowed,
      .reason = result.reason,
  };
}

}  // namespace dasall::contracts
