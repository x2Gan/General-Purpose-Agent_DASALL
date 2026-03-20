#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prompt/PromptComposeRequest.h"

namespace dasall::contracts {

enum class PromptSpecBoundaryDecision {
  AllowField = 0,
  RejectReleaseLifecycleField = 1,
  RejectRuntimeComposeField = 2,
  RejectComposeResultField = 3,
};

struct PromptSpecBoundaryResult {
  bool allowed = true;
  PromptSpecBoundaryDecision decision = PromptSpecBoundaryDecision::AllowField;
  std::string_view reason = "prompt spec field is allowed";
};

inline constexpr std::array<std::string_view, 5> kPromptSpecReleaseLifecycleForbiddenFields = {
    "version",
    "eval_status",
    "release_scope",
    "rollback_from",
    "trusted_source",
};

inline constexpr std::array<std::string_view, 4> kPromptSpecRuntimeComposeForbiddenFields = {
    "context_packet_id",
    "visible_tools",
    "model_route",
    "response_format",
};

inline constexpr std::array<std::string_view, 4> kPromptSpecComposeResultForbiddenFields = {
    "messages",
    "estimated_tokens",
    "selected_version",
    "composition_warnings",
};

constexpr PromptSpecBoundaryResult evaluate_prompt_spec_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kPromptSpecReleaseLifecycleForbiddenFields) {
    if (field_name == forbidden_field) {
      return PromptSpecBoundaryResult{
          .allowed = false,
          .decision = PromptSpecBoundaryDecision::RejectReleaseLifecycleField,
          .reason = "prompt spec must not contain release lifecycle fields",
      };
    }
  }

  for (const auto forbidden_field : kPromptSpecRuntimeComposeForbiddenFields) {
    if (field_name == forbidden_field) {
      return PromptSpecBoundaryResult{
          .allowed = false,
          .decision = PromptSpecBoundaryDecision::RejectRuntimeComposeField,
          .reason = "prompt spec must not contain runtime compose request fields",
      };
    }
  }

  for (const auto forbidden_field : kPromptSpecComposeResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return PromptSpecBoundaryResult{
          .allowed = false,
          .decision = PromptSpecBoundaryDecision::RejectComposeResultField,
          .reason = "prompt spec must not contain compose result fields",
      };
    }
  }

  return PromptSpecBoundaryResult{};
}

constexpr bool is_allowed_prompt_spec_field(std::string_view field_name) {
  return evaluate_prompt_spec_field_boundary(field_name).allowed;
}

struct PromptSpec {
  std::optional<std::string> prompt_id;
  std::optional<CompositionStage> stage;
  std::optional<std::vector<std::string>> template_slots;

  std::optional<std::vector<std::string>> task_types;
  std::optional<std::string> language;
  std::optional<std::string> model_family;
  std::optional<std::string> output_schema_ref;
  std::optional<std::vector<std::string>> tool_hints;
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts