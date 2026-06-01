#pragma once

#include <cstddef>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "prompt/PromptRelease.h"
#include "prompt/PromptSpec.h"

namespace dasall::contracts {

struct PromptSpecGuardResult {
  bool ok = false;
  std::string_view reason = "prompt spec validation failed";
};

struct PromptReleaseGuardResult {
  bool ok = false;
  std::string_view reason = "prompt release validation failed";
};

inline PromptSpecGuardResult validate_prompt_spec_required_fields(
    const PromptSpec& spec) {
  if (!has_non_empty_value(spec.prompt_id)) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "prompt_id is required and must be non-empty",
    };
  }

  if (!spec.stage.has_value() || *spec.stage == CompositionStage::Unspecified) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "stage is required and must not be Unspecified",
    };
  }

  if (!spec.template_slots.has_value() || spec.template_slots->empty()) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "template_slots are required and must contain at least one item",
    };
  }

  for (const auto& slot : *spec.template_slots) {
    if (slot.empty()) {
      return PromptSpecGuardResult{
          .ok = false,
          .reason = "template_slots must not contain empty-string elements",
      };
    }
  }

  return PromptSpecGuardResult{
      .ok = true,
      .reason = "all required prompt spec fields present",
  };
}

inline PromptSpecGuardResult validate_prompt_spec_boundary(const PromptSpec& spec) {
  auto required_result = validate_prompt_spec_required_fields(spec);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_stage = static_cast<int>(*spec.stage);
  if (raw_stage < static_cast<int>(CompositionStage::Planning) ||
      raw_stage > static_cast<int>(CompositionStage::Perception)) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "stage value is outside the known CompositionStage enum range",
    };
  }

  return PromptSpecGuardResult{
      .ok = true,
      .reason = "prompt spec boundary validation passed",
  };
}

inline PromptSpecGuardResult validate_prompt_spec_field_rules(const PromptSpec& spec) {
  auto boundary_result = validate_prompt_spec_boundary(spec);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (spec.language.has_value() && spec.language->empty()) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "language must be non-empty when present",
    };
  }

  if (spec.model_family.has_value() && spec.model_family->empty()) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "model_family must be non-empty when present",
    };
  }

  if (spec.output_schema_ref.has_value() && spec.output_schema_ref->empty()) {
    return PromptSpecGuardResult{
        .ok = false,
        .reason = "output_schema_ref must be non-empty when present",
    };
  }

  const auto validate_string_vector = [](const std::optional<std::vector<std::string>>& values,
                                         std::string_view empty_reason,
                                         std::string_view element_reason,
                                         std::string_view duplicate_reason)
      -> PromptSpecGuardResult {
    if (!values.has_value()) {
      return PromptSpecGuardResult{.ok = true, .reason = "vector absent"};
    }
    if (values->empty()) {
      return PromptSpecGuardResult{.ok = false, .reason = empty_reason};
    }
    for (const auto& value : *values) {
      if (value.empty()) {
        return PromptSpecGuardResult{.ok = false, .reason = element_reason};
      }
    }
    for (std::size_t index = 0; index < values->size(); ++index) {
      for (std::size_t probe = index + 1; probe < values->size(); ++probe) {
        if ((*values)[index] == (*values)[probe]) {
          return PromptSpecGuardResult{.ok = false, .reason = duplicate_reason};
        }
      }
    }
    return PromptSpecGuardResult{.ok = true, .reason = "vector valid"};
  };

  auto task_types_result = validate_string_vector(
      spec.task_types,
      "task_types must contain at least one item when present",
      "task_types must not contain empty-string elements",
      "task_types must not contain duplicate items");
  if (!task_types_result.ok) {
    return task_types_result;
  }

  auto template_slots_result = validate_string_vector(
      spec.template_slots,
      "template_slots must contain at least one item when present",
      "template_slots must not contain empty-string elements",
      "template_slots must not contain duplicate items");
  if (!template_slots_result.ok) {
    return template_slots_result;
  }

  auto tool_hints_result = validate_string_vector(
      spec.tool_hints,
      "tool_hints must contain at least one item when present",
      "tool_hints must not contain empty-string elements",
      "tool_hints must not contain duplicate items");
  if (!tool_hints_result.ok) {
    return tool_hints_result;
  }

  auto tags_result = validate_string_vector(
      spec.tags,
      "tags must contain at least one item when present",
      "tags must not contain empty-string elements",
      "tags must not contain duplicate items");
  if (!tags_result.ok) {
    return tags_result;
  }

  return PromptSpecGuardResult{
      .ok = true,
      .reason = "prompt spec field rules validation passed",
  };
}

inline PromptReleaseGuardResult validate_prompt_release_required_fields(
    const PromptRelease& release) {
  if (!has_non_empty_value(release.prompt_id)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "prompt_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(release.version)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "version is required and must be non-empty",
    };
  }

  if (!release.stage.has_value() || *release.stage == CompositionStage::Unspecified) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "stage is required and must not be Unspecified",
    };
  }

  if (!release.eval_status.has_value() || *release.eval_status == PromptEvalStatus::Unspecified) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "eval_status is required and must not be Unspecified",
    };
  }

  if (!has_non_empty_value(release.release_scope)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "release_scope is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(release.system_instructions)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "system_instructions is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(release.task_template)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "task_template is required and must be non-empty",
    };
  }

  return PromptReleaseGuardResult{
      .ok = true,
      .reason = "all required prompt release fields present",
  };
}

inline PromptReleaseGuardResult validate_prompt_release_boundary(
    const PromptRelease& release) {
  auto required_result = validate_prompt_release_required_fields(release);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_stage = static_cast<int>(*release.stage);
  if (raw_stage < static_cast<int>(CompositionStage::Planning) ||
      raw_stage > static_cast<int>(CompositionStage::Perception)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "stage value is outside the known CompositionStage enum range",
    };
  }

  const int raw_eval_status = static_cast<int>(*release.eval_status);
  if (raw_eval_status < static_cast<int>(PromptEvalStatus::Draft) ||
      raw_eval_status > static_cast<int>(PromptEvalStatus::Deprecated)) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "eval_status value is outside the known PromptEvalStatus enum range",
    };
  }

  return PromptReleaseGuardResult{
      .ok = true,
      .reason = "prompt release boundary validation passed",
  };
}

inline PromptReleaseGuardResult validate_prompt_release_field_rules(
    const PromptRelease& release) {
  auto boundary_result = validate_prompt_release_boundary(release);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (release.output_schema_ref.has_value() && release.output_schema_ref->empty()) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "output_schema_ref must be non-empty when present",
    };
  }

  if (release.rollback_from.has_value() && release.rollback_from->empty()) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "rollback_from must be non-empty when present",
    };
  }

  if (release.trusted_source.has_value() && release.trusted_source->empty()) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "trusted_source must be non-empty when present",
    };
  }

  const auto validate_string_vector = [](const std::optional<std::vector<std::string>>& values,
                                         std::string_view empty_reason,
                                         std::string_view element_reason,
                                         std::string_view duplicate_reason)
      -> PromptReleaseGuardResult {
    if (!values.has_value()) {
      return PromptReleaseGuardResult{.ok = true, .reason = "vector absent"};
    }
    if (values->empty()) {
      return PromptReleaseGuardResult{.ok = false, .reason = empty_reason};
    }
    for (const auto& value : *values) {
      if (value.empty()) {
        return PromptReleaseGuardResult{.ok = false, .reason = element_reason};
      }
    }
    for (std::size_t index = 0; index < values->size(); ++index) {
      for (std::size_t probe = index + 1; probe < values->size(); ++probe) {
        if ((*values)[index] == (*values)[probe]) {
          return PromptReleaseGuardResult{.ok = false, .reason = duplicate_reason};
        }
      }
    }
    return PromptReleaseGuardResult{.ok = true, .reason = "vector valid"};
  };

  auto few_shot_refs_result = validate_string_vector(
      release.few_shot_refs,
      "few_shot_refs must contain at least one item when present",
      "few_shot_refs must not contain empty-string elements",
      "few_shot_refs must not contain duplicate items");
  if (!few_shot_refs_result.ok) {
    return few_shot_refs_result;
  }

  auto policy_notes_result = validate_string_vector(
      release.policy_notes,
      "policy_notes must contain at least one item when present",
      "policy_notes must not contain empty-string elements",
      "policy_notes must not contain duplicate items");
  if (!policy_notes_result.ok) {
    return policy_notes_result;
  }

  auto tags_result = validate_string_vector(
      release.tags,
      "tags must contain at least one item when present",
      "tags must not contain empty-string elements",
      "tags must not contain duplicate items");
  if (!tags_result.ok) {
    return tags_result;
  }

  if (release.rollback_from.has_value() && *release.rollback_from == *release.version) {
    return PromptReleaseGuardResult{
        .ok = false,
        .reason = "rollback_from must not equal version",
    };
  }

  return PromptReleaseGuardResult{
      .ok = true,
      .reason = "prompt release field rules validation passed",
  };
}

}  // namespace dasall::contracts