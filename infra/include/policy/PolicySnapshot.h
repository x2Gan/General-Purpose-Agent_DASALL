#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PolicyBundle.h"

namespace dasall::infra::policy {

struct PolicySnapshot {
  std::string snapshot_id;
  std::uint64_t generation = 0;
  std::string version;
  PolicyMode mode = PolicyMode::Unspecified;
  std::vector<PolicyRuleDescriptor> effective_rules;
  std::string created_at;
  std::vector<std::string> source_chain;
  std::string last_known_good_ref;

  [[nodiscard]] bool is_valid() const {
    if (snapshot_id.empty() || generation == 0 || version.empty() ||
        mode == PolicyMode::Unspecified || created_at.empty() || effective_rules.empty() ||
        source_chain.empty()) {
      return false;
    }

    for (const auto& rule : effective_rules) {
      if (!rule.is_valid()) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool can_roll_back() const {
    return is_valid() && !last_known_good_ref.empty();
  }
};

}  // namespace dasall::infra::policy