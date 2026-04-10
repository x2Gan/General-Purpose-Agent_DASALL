#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dasall::llm {

struct LLMAdapterConfig {
  std::string adapter_id;
  std::string adapter_family;
  std::string base_url;
  std::string auth_ref;
  std::vector<std::string> header_refs;
  std::uint32_t timeout_ms = 30000;
  std::uint32_t max_retries = 1;
  std::vector<std::string> capability_tags;
};

}  // namespace dasall::llm
