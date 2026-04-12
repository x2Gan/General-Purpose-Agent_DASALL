#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dasall::llm::prompt {

struct PromptPolicyInput {
  std::string profile_id;
  std::vector<std::string> allowed_prompt_releases;
  std::vector<std::string> trusted_sources;
  std::vector<std::string> tool_visibility_rules;
  std::uint32_t render_budget_tokens = 0;
  std::string active_scene;
  std::string active_persona;
  std::string selected_release_scope;
  std::string selected_trusted_source;
  std::vector<std::string> visible_tools;
};

}  // namespace dasall::llm::prompt