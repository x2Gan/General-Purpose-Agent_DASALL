#pragma once

#include <string>
#include <vector>

namespace dasall::llm::prompt {

struct PromptQuery {
  std::string stage;
  std::string task_type;
  std::string language;
  std::string model_family;
  std::string scene_id;
  std::string persona_id;
  std::string profile_id;
  std::vector<std::string> available_tools;
  std::vector<std::string> trusted_sources;
};

}  // namespace dasall::llm::prompt
