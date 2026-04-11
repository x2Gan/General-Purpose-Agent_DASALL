#pragma once

#include <string>
#include <vector>

namespace dasall::llm {

struct ProviderDescriptor {
  std::string provider_id;
  std::string adapter_family;
  std::string api_family;
  std::string base_url;
  std::string auth_ref;
  std::vector<std::string> header_refs;
  std::vector<std::string> capability_tags;
  std::string source_version;
};

}  // namespace dasall::llm
