#pragma once

#include <string>

namespace dasall::llm::prompt {

struct PromptAssetMetadata {
  std::string prompt_release_id;
  std::string package_id;
  std::string content_hash;
  std::string source_layer;
  std::string source_uri;

  [[nodiscard]] bool has_consistent_values() const {
    return !prompt_release_id.empty() && !package_id.empty() &&
           !content_hash.empty() && !source_layer.empty() && !source_uri.empty();
  }
};

}  // namespace dasall::llm::prompt