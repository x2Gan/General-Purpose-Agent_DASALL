#include "PolicySchemaValidator.h"

#include <cstddef>
#include <string>
#include <utility>

namespace dasall::infra::policy {
namespace {

constexpr const char* kSupportedSchemaVersion = "1";

void append_error(ValidationReport& report,
                  std::string error,
                  std::string invalid_rule_id,
                  std::string field_path) {
  report.blocking_errors.push_back(std::move(error));
  if (!invalid_rule_id.empty()) {
    report.invalid_rule_ids.push_back(std::move(invalid_rule_id));
  }
  report.field_paths.push_back(std::move(field_path));
}

std::string make_rule_label(const PolicyRuleDescriptor& rule, std::size_t rule_index) {
  if (!rule.rule_id.empty()) {
    return rule.rule_id;
  }

  return std::string("rule[") + std::to_string(rule_index) + "]";
}

void validate_rule_fields(const PolicyRuleDescriptor& rule,
                         std::size_t rule_index,
                         const std::string& field_prefix,
                         ValidationReport& report) {
  const std::string rule_label = make_rule_label(rule, rule_index);

  if (rule.rule_id.empty()) {
    append_error(report,
                 "policy_rule_field_missing",
                 rule_label,
                 field_prefix + ".rule_id");
  }
  if (rule.domain == PolicyDomain::Unspecified) {
    append_error(report,
                 "policy_rule_domain_unsupported",
                 rule_label,
                 field_prefix + ".domain");
  }
  if (rule.subject.empty()) {
    append_error(report,
                 "policy_rule_field_missing",
                 rule_label,
                 field_prefix + ".subject");
  }
  if (rule.action.empty()) {
    append_error(report,
                 "policy_rule_field_missing",
                 rule_label,
                 field_prefix + ".action");
  }
  if (rule.target_selector.empty()) {
    append_error(report,
                 "policy_rule_field_missing",
                 rule_label,
                 field_prefix + ".target_selector");
  }
  if (rule.effect == PolicyEffect::Unspecified) {
    append_error(report,
                 "policy_rule_effect_invalid",
                 rule_label,
                 field_prefix + ".effect");
  }
  if (rule.mode == PolicyMode::Unspecified) {
    append_error(report,
                 "policy_rule_mode_invalid",
                 rule_label,
                 field_prefix + ".mode");
  }
  if (rule.reason_code.empty()) {
    append_error(report,
                 "policy_rule_field_missing",
                 rule_label,
                 field_prefix + ".reason_code");
  }

  for (std::size_t condition_index = 0; condition_index < rule.conditions.size(); ++condition_index) {
    if (rule.conditions[condition_index].empty()) {
      append_error(report,
                   "policy_rule_condition_invalid",
                   rule_label,
                   field_prefix + ".conditions[" + std::to_string(condition_index) + "]");
    }
  }
}

void validate_patch_operation(const PolicyPatchOperation& operation,
                             std::size_t operation_index,
                             ValidationReport& report) {
  const std::string operation_prefix =
      std::string("operations[") + std::to_string(operation_index) + "]";

  switch (operation.operation) {
    case PolicyPatchOperationType::AddRule:
    case PolicyPatchOperationType::ReplaceRule:
      if (!operation.rule.has_value()) {
        append_error(report,
                     "policy_patch_rule_missing",
                     operation.rule_id,
                     operation_prefix + ".rule");
        return;
      }

      validate_rule_fields(*operation.rule, operation_index, operation_prefix + ".rule", report);
      return;
    case PolicyPatchOperationType::RemoveRule:
      if (operation.rule_id.empty()) {
        append_error(report,
                     "policy_patch_rule_missing",
                     {},
                     operation_prefix + ".rule_id");
      }
      return;
    case PolicyPatchOperationType::UpdateMode:
      if (operation.mode == PolicyMode::Unspecified) {
        append_error(report,
                     "policy_patch_mode_invalid",
                     operation.rule_id,
                     operation_prefix + ".mode");
      }
      return;
    case PolicyPatchOperationType::Unspecified:
      append_error(report,
                   "policy_patch_operation_invalid",
                   operation.rule_id,
                   operation_prefix + ".operation");
      return;
  }
}

}  // namespace

ValidationReport PolicySchemaValidator::validate_bundle(const PolicyBundle& bundle) const {
  ValidationReport report;

  if (bundle.bundle_id.empty()) {
    append_error(report, "policy_bundle_field_missing", "bundle", "bundle_id");
  }
  if (bundle.schema_version.empty()) {
    append_error(report, "policy_bundle_field_missing", "bundle", "schema_version");
  } else if (bundle.schema_version != kSupportedSchemaVersion) {
    append_error(report, "policy_schema_unsupported", "bundle", "schema_version");
  }
  if (bundle.source.empty()) {
    append_error(report, "policy_bundle_field_missing", "bundle", "source");
  }
  if (bundle.checksum.empty()) {
    append_error(report, "policy_bundle_field_missing", "bundle", "checksum");
  }
  if (bundle.generated_at.empty()) {
    append_error(report, "policy_bundle_field_missing", "bundle", "generated_at");
  }
  if (bundle.rules.empty()) {
    append_error(report, "policy_bundle_rules_missing", "bundle", "rules");
    return report;
  }

  for (std::size_t rule_index = 0; rule_index < bundle.rules.size(); ++rule_index) {
    validate_rule_fields(bundle.rules[rule_index],
                         rule_index,
                         std::string("rules[") + std::to_string(rule_index) + "]",
                         report);
  }

  return report;
}

ValidationReport PolicySchemaValidator::validate_patch(const PolicySnapshot& current_snapshot,
                                                       const PolicyPatch& patch) const {
  ValidationReport report;

  if (!current_snapshot.is_valid()) {
    append_error(report,
                 "policy_snapshot_invalid",
                 current_snapshot.snapshot_id,
                 "current_snapshot");
    return report;
  }

  if (patch.patch_id.empty()) {
    append_error(report, "policy_patch_field_missing", "patch", "patch_id");
  }
  if (patch.base_generation == 0) {
    append_error(report, "patch_base_mismatch", "patch", "base_generation");
  } else if (patch.base_generation != current_snapshot.generation) {
    append_error(report, "patch_base_mismatch", "patch", "base_generation");
  }
  if (patch.operations.empty()) {
    append_error(report, "policy_patch_field_missing", "patch", "operations");
  }
  if (patch.actor.empty()) {
    append_error(report, "policy_patch_field_missing", "patch", "actor");
  }
  if (patch.reason.empty()) {
    append_error(report, "policy_patch_field_missing", "patch", "reason");
  }

  for (std::size_t operation_index = 0; operation_index < patch.operations.size(); ++operation_index) {
    validate_patch_operation(patch.operations[operation_index], operation_index, report);
  }

  return report;
}

}  // namespace dasall::infra::policy