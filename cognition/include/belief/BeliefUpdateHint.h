#pragma once

#include <string>
#include <vector>

namespace dasall::cognition::belief {

struct BeliefUpdateHint {
  std::vector<std::string> confirmed_facts;
  std::vector<std::string> evidence_refs;
  std::string merge_mode = "append";
};

}  // namespace dasall::cognition::belief