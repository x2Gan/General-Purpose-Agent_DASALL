#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::infra::policy {

enum class PolicyDomain {
  Unspecified = 0,
  SecretAccess = 1,
  PluginLoad = 2,
  DiagnosticsCommand = 3,
  OTAApply = 4,
  OTARollback = 5,
  PolicyAdmin = 6,
};

enum class PolicyEffect {
  Unspecified = 0,
  Allow = 1,
  Deny = 2,
  RequireConfirmation = 3,
  Observe = 4,
};

enum class PolicyMode {
  Unspecified = 0,
  Enforced = 1,
  Compatibility = 2,
};

[[nodiscard]] constexpr std::uint32_t policy_effect_precedence(PolicyEffect effect) {
  switch (effect) {
    case PolicyEffect::Deny:
      return 0;
    case PolicyEffect::RequireConfirmation:
      return 1;
    case PolicyEffect::Allow:
      return 2;
    case PolicyEffect::Observe:
      return 3;
    case PolicyEffect::Unspecified:
      break;
  }

  return 4;
}

[[nodiscard]] inline std::string_view to_string(PolicyEffect effect) {
  switch (effect) {
    case PolicyEffect::Allow:
      return "allow";
    case PolicyEffect::Deny:
      return "deny";
    case PolicyEffect::RequireConfirmation:
      return "require_confirmation";
    case PolicyEffect::Observe:
      return "observe";
    case PolicyEffect::Unspecified:
      break;
  }

  return "unspecified";
}

struct PolicyRuleDescriptor {
  std::string rule_id;
  PolicyDomain domain = PolicyDomain::Unspecified;
  std::string subject;
  std::string action;
  std::string target_selector;
  PolicyEffect effect = PolicyEffect::Unspecified;
  std::uint32_t priority = 0;
  PolicyMode mode = PolicyMode::Unspecified;
  std::vector<std::string> conditions;
  std::string reason_code;

  [[nodiscard]] bool is_valid() const {
    return !rule_id.empty() && domain != PolicyDomain::Unspecified && !subject.empty() &&
           !action.empty() && !target_selector.empty() &&
           effect != PolicyEffect::Unspecified && mode != PolicyMode::Unspecified &&
           !reason_code.empty();
  }
};

struct PolicyBundle {
  std::string bundle_id;
  std::string schema_version;
  std::string source;
  std::string checksum;
  std::vector<PolicyRuleDescriptor> rules;
  std::string generated_at;

  [[nodiscard]] bool is_valid() const {
    if (bundle_id.empty() || schema_version.empty() || source.empty() || checksum.empty() ||
        generated_at.empty() || rules.empty()) {
      return false;
    }

    for (const auto& rule : rules) {
      if (!rule.is_valid()) {
        return false;
      }
    }

    return true;
  }
};

}  // namespace dasall::infra::policy