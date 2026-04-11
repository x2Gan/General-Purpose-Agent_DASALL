#pragma once

#include <string>
#include <vector>

namespace dasall::llm::prompt {

struct PromptPolicyConfig {
  std::vector<std::string> default_allowed_releases;
  std::vector<std::string> default_trusted_sources;
  bool deny_on_missing_allowlist = true;
};

}  // namespace dasall::llm::prompt