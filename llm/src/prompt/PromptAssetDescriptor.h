#pragma once

#include <string>
#include <vector>

#include "prompt/PromptRelease.h"
#include "prompt/PromptSpec.h"

namespace dasall::llm::prompt {

struct PromptAssetDescriptor {
  dasall::contracts::PromptSpec spec;
  dasall::contracts::PromptRelease release;
  std::string package_id;
  std::string schema_version;
  std::string min_loader_version;
  std::string source_layer;
  std::string source_uri;
  std::string content_hash;
  std::string scene_id;
  std::string persona_id;
  std::vector<std::string> profile_tags;
  bool is_default_release = false;

  [[nodiscard]] bool has_consistent_values() const {
    if (package_id.empty() || schema_version != "1" || min_loader_version != "1" ||
        source_layer.empty() || source_uri.empty() || content_hash.empty() ||
        !spec.prompt_id.has_value() || !spec.stage.has_value() ||
        !spec.output_schema_ref.has_value() || !release.prompt_id.has_value() ||
        !release.version.has_value() || !release.stage.has_value() ||
        !release.eval_status.has_value() || !release.release_scope.has_value() ||
        !release.system_instructions.has_value() || !release.task_template.has_value() ||
        !release.output_schema_ref.has_value() || !release.trusted_source.has_value() ||
        !release.tags.has_value() || release.tags->empty()) {
      return false;
    }

    if (*spec.prompt_id != *release.prompt_id || *spec.stage != *release.stage ||
        *spec.output_schema_ref != *release.output_schema_ref) {
      return false;
    }

    return !release.system_instructions->empty() && !release.task_template->empty();
  }
};

}  // namespace dasall::llm::prompt