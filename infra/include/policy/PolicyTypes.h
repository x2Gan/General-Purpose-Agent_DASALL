#pragma once

#include <cstdint>
#include <optional>
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

[[nodiscard]] inline std::string_view to_string(PolicyDomain domain) {
  switch (domain) {
    case PolicyDomain::SecretAccess:
      return "secret_access";
    case PolicyDomain::PluginLoad:
      return "plugin_load";
    case PolicyDomain::DiagnosticsCommand:
      return "diagnostics_command";
    case PolicyDomain::OTAApply:
      return "ota_apply";
    case PolicyDomain::OTARollback:
      return "ota_rollback";
    case PolicyDomain::PolicyAdmin:
      return "policy_admin";
    case PolicyDomain::Unspecified:
      break;
  }

  return "unspecified";
}

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

struct ValidationReport {
  std::vector<std::string> blocking_errors;
  std::vector<std::string> warnings;
  std::vector<std::string> invalid_rule_ids;
  std::vector<std::string> field_paths;

  [[nodiscard]] bool has_blocking_errors() const {
    return !blocking_errors.empty();
  }
};

enum class PolicyPatchOperationType {
  Unspecified = 0,
  AddRule = 1,
  ReplaceRule = 2,
  RemoveRule = 3,
  UpdateMode = 4,
};

[[nodiscard]] inline std::string_view to_string(PolicyPatchOperationType operation) {
  switch (operation) {
    case PolicyPatchOperationType::AddRule:
      return "add_rule";
    case PolicyPatchOperationType::ReplaceRule:
      return "replace_rule";
    case PolicyPatchOperationType::RemoveRule:
      return "remove_rule";
    case PolicyPatchOperationType::UpdateMode:
      return "update_mode";
    case PolicyPatchOperationType::Unspecified:
      break;
  }

  return "unspecified";
}

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
