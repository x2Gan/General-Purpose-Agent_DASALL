#pragma once

#include <string>
#include <vector>

namespace dasall::llm {

struct ResolvedModelRoute {
  std::string stage;
  std::string primary_route;
  std::vector<std::string> fallback_routes;
  bool streaming_enabled = false;
};

}  // namespace dasall::llm
