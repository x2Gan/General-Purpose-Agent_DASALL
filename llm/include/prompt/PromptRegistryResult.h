#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ResultCode.h"
#include "prompt/PromptRelease.h"

namespace dasall::llm::prompt {

struct PromptRegistryResult {
  std::optional<dasall::contracts::ResultCode> code;
  std::optional<dasall::contracts::PromptRelease> release;
  std::string selected_prompt_id;
  std::string selected_version;
  std::string selection_reason;
  std::vector<std::string> trusted_sources_matched;

  [[nodiscard]] bool has_consistent_values() const {
    const bool has_release = release.has_value();
    const bool has_failure = code.has_value();
    const bool has_prompt_identity =
        !selected_prompt_id.empty() || !selected_version.empty();

    if (has_release == has_failure) {
      return false;
    }

    if (selected_prompt_id.empty() != selected_version.empty()) {
      return false;
    }

    if (has_release) {
      if (!has_prompt_identity || selection_reason.empty()) {
        return false;
      }

      if (!release->prompt_id.has_value() || !release->version.has_value()) {
        return false;
      }

      if (*release->prompt_id != selected_prompt_id ||
          *release->version != selected_version) {
        return false;
      }
    }

    if (has_failure && has_prompt_identity) {
      return false;
    }

    if (has_failure && !trusted_sources_matched.empty()) {
      return false;
    }

    return true;
  }
};

}  // namespace dasall::llm::prompt
