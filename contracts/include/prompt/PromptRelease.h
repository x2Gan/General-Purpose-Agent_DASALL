#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prompt/PromptComposeRequest.h"

namespace dasall::contracts {

enum class PromptEvalStatus {
  Unspecified = 0,
  Draft = 1,
  Experiment = 2,
  Canary = 3,
  Stable = 4,
  Deprecated = 5,
};

enum class PromptReleaseBoundaryDecision {
  AllowField = 0,
  RejectContextOwnershipField = 1,
  RejectRuntimeComposeField = 2,
  RejectWritebackField = 3,
};

struct PromptReleaseBoundaryResult {
  bool allowed = true;
  PromptReleaseBoundaryDecision decision = PromptReleaseBoundaryDecision::AllowField;
  std::string_view reason = "prompt release field is allowed";
};

inline constexpr std::array<std::string_view, 4> kPromptReleaseContextOwnershipForbiddenFields = {
    "context_packet_id",
    "memory_snapshot",
    "retrieval_candidates",
    "knowledge_fragments",
};

inline constexpr std::array<std::string_view, 3> kPromptReleaseRuntimeComposeForbiddenFields = {
    "visible_tools",
    "model_route",
    "response_format",
};

inline constexpr std::array<std::string_view, 4> kPromptReleaseWritebackForbiddenFields = {
    "messages",
    "estimated_tokens",
    "memory_write_back",
    "context_update",
};

constexpr PromptReleaseBoundaryResult evaluate_prompt_release_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kPromptReleaseContextOwnershipForbiddenFields) {
    if (field_name == forbidden_field) {
      return PromptReleaseBoundaryResult{
          .allowed = false,
          .decision = PromptReleaseBoundaryDecision::RejectContextOwnershipField,
          .reason = "prompt release must not contain context ownership fields",
      };
    }
  }

  for (const auto forbidden_field : kPromptReleaseRuntimeComposeForbiddenFields) {
    if (field_name == forbidden_field) {
      return PromptReleaseBoundaryResult{
          .allowed = false,
          .decision = PromptReleaseBoundaryDecision::RejectRuntimeComposeField,
          .reason = "prompt release must not contain runtime compose control fields",
      };
    }
  }

  for (const auto forbidden_field : kPromptReleaseWritebackForbiddenFields) {
    if (field_name == forbidden_field) {
      return PromptReleaseBoundaryResult{
          .allowed = false,
          .decision = PromptReleaseBoundaryDecision::RejectWritebackField,
          .reason = "prompt release must not contain compose-result or write-back fields",
      };
    }
  }

  return PromptReleaseBoundaryResult{};
}

constexpr bool is_allowed_prompt_release_field(std::string_view field_name) {
  return evaluate_prompt_release_field_boundary(field_name).allowed;
}

struct PromptRelease {
  std::optional<std::string> prompt_id;
  std::optional<std::string> version;
  std::optional<CompositionStage> stage;
  std::optional<PromptEvalStatus> eval_status;
  std::optional<std::string> release_scope;
  std::optional<std::string> system_instructions;
  std::optional<std::string> task_template;

  std::optional<std::string> output_schema_ref;
  std::optional<std::vector<std::string>> few_shot_refs;
  std::optional<std::vector<std::string>> policy_notes;
  std::optional<std::string> rollback_from;
  std::optional<std::string> trusted_source;
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts