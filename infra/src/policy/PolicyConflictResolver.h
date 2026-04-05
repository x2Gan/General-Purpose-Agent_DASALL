#pragma once

#include <string>
#include <vector>

#include "policy/PolicyErrors.h"
#include "policy/PolicyTypes.h"

namespace dasall::infra::policy {

struct ConflictResolutionResult {
  bool resolved = false;
  std::string priority_order = "deny-first";
  std::vector<PolicyRuleDescriptor> effective_rules;
  std::vector<std::string> conflict_rule_ids;
  std::vector<std::string> warnings;
  PolicyErrorCode error_code = PolicyErrorCode::ConflictUnresolved;
  std::string reason_code;

  [[nodiscard]] bool has_conflict() const {
    return !conflict_rule_ids.empty();
  }
};

class PolicyConflictResolver final {
 public:
  [[nodiscard]] ConflictResolutionResult resolve(const PolicyBundle& bundle) const;
};

}  // namespace dasall::infra::policy