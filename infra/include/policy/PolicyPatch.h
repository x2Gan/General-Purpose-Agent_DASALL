#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "PolicyBundle.h"

namespace dasall::infra::policy {

enum class PolicyPatchOperationType {
  Unspecified = 0,
  AddRule = 1,
  ReplaceRule = 2,
  RemoveRule = 3,
  UpdateMode = 4,
};

struct PolicyPatchOperation {
  PolicyPatchOperationType operation = PolicyPatchOperationType::Unspecified;
  std::string rule_id;
  std::optional<PolicyRuleDescriptor> rule;
  PolicyMode mode = PolicyMode::Unspecified;

  [[nodiscard]] bool is_valid() const {
    switch (operation) {
      case PolicyPatchOperationType::AddRule:
      case PolicyPatchOperationType::ReplaceRule:
        return rule.has_value() && rule->is_valid();
      case PolicyPatchOperationType::RemoveRule:
        return !rule_id.empty();
      case PolicyPatchOperationType::UpdateMode:
        return mode != PolicyMode::Unspecified;
      case PolicyPatchOperationType::Unspecified:
        break;
    }

    return false;
  }
};

struct PolicyPatch {
  std::string patch_id;
  std::uint64_t base_generation = 0;
  std::vector<PolicyPatchOperation> operations;
  std::string actor;
  std::string reason;

  [[nodiscard]] bool is_valid() const {
    if (patch_id.empty() || base_generation == 0 || operations.empty() || actor.empty() ||
        reason.empty()) {
      return false;
    }

    for (const auto& operation : operations) {
      if (!operation.is_valid()) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace dasall::infra::policy