#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

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

struct PolicyOpResult {
  bool applied = false;
  bool rolled_back = false;
  bool dry_run = false;
  std::string snapshot_id;
  std::uint64_t generation = 0;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static PolicyOpResult success(std::string snapshot_id,
                                              std::uint64_t generation,
                                              bool rolled_back = false,
                                              bool dry_run = false) {
    return PolicyOpResult{
        .applied = true,
        .rolled_back = rolled_back,
        .dry_run = dry_run,
        .snapshot_id = std::move(snapshot_id),
        .generation = generation,
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static PolicyOpResult failure(contracts::ResultCode result_code,
                                              std::string message,
                                              std::string stage,
                                              std::string source_ref) {
    return PolicyOpResult{
        .applied = false,
        .rolled_back = false,
        .dry_run = false,
        .snapshot_id = {},
        .generation = 0,
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.policy",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return applied;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

}  // namespace dasall::infra::policy
