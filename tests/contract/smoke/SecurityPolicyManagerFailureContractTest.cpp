#include <exception>
#include <iostream>
#include <string>

#include "policy/PolicyErrors.h"
#include "policy/PolicySchemaValidator.h"
#include "policy/PolicySnapshotStore.h"
#include "policy/SecurityPolicyManager.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_patch_gate_rule(bool dry_run_required,
                                                                 std::uint32_t safe_mode_threshold) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("policy-loader-admin-patch-gate"),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("infra.policy"),
      .action = std::string("apply_patch"),
      .target_selector = std::string("policy:runtime_patch"),
      .effect = dasall::infra::policy::PolicyEffect::RequireConfirmation,
      .priority = 1,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {
          std::string("dry_run_required=") + (dry_run_required ? "true" : "false"),
          std::string("safe_mode_threshold=") + std::to_string(safe_mode_threshold),
          std::string("priority_order=deny-first"),
      },
      .reason_code = std::string("policy_patch_confirmation_required"),
  };
}

dasall::infra::policy::PolicyBundle make_bundle(std::uint32_t safe_mode_threshold = 2) {
  return dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-manager-contract-bundle"),
      .schema_version = std::string("1"),
      .source = std::string("source_id=defaults;version=defaults@resolved"),
      .checksum = std::string("sha256:policy-manager-contract"),
      .rules = {
          make_patch_gate_rule(true, safe_mode_threshold),
          dasall::infra::policy::PolicyRuleDescriptor{
              .rule_id = std::string("policy-loader-default-effect"),
              .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
              .subject = std::string("infra.policy"),
              .action = std::string("evaluate_default"),
              .target_selector = std::string("policy:default_effect"),
              .effect = dasall::infra::policy::PolicyEffect::Deny,
              .priority = 2,
              .mode = dasall::infra::policy::PolicyMode::Enforced,
              .conditions = {std::string("default_effect=deny"), std::string("priority_order=deny-first")},
              .reason_code = std::string("policy_default_deny"),
          },
          dasall::infra::policy::PolicyRuleDescriptor{
              .rule_id = std::string("plugin-policy-rule"),
              .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
              .subject = std::string("ops-user"),
              .action = std::string("load"),
              .target_selector = std::string("manifest:plugin.echo"),
              .effect = dasall::infra::policy::PolicyEffect::Allow,
              .priority = 5,
              .mode = dasall::infra::policy::PolicyMode::Enforced,
              .conditions = {std::string("priority_order=deny-first")},
              .reason_code = std::string("plugin_allowed"),
          },
      },
      .generated_at = std::string("2026-04-05T19:05:00Z"),
  };
}

dasall::infra::policy::PolicyPatch make_patch(std::uint64_t base_generation) {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("policy-manager-contract-patch"),
      .base_generation = base_generation,
      .operations = {dasall::infra::policy::PolicyPatchOperation{
          .operation = dasall::infra::policy::PolicyPatchOperationType::ReplaceRule,
          .rule_id = std::string("plugin-policy-rule"),
          .rule = dasall::infra::policy::PolicyRuleDescriptor{
              .rule_id = std::string("plugin-policy-rule"),
              .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
              .subject = std::string("ops-user"),
              .action = std::string("load"),
              .target_selector = std::string("manifest:plugin.echo"),
              .effect = dasall::infra::policy::PolicyEffect::Deny,
              .priority = 5,
              .mode = dasall::infra::policy::PolicyMode::Enforced,
              .conditions = {std::string("priority_order=deny-first")},
              .reason_code = std::string("plugin_denied"),
          },
          .mode = dasall::infra::policy::PolicyMode::Unspecified,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("contract-check"),
  };
}

void test_security_policy_manager_rejections_stay_in_policy_failure_domain() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);

  assert_true(manager.load_policy(make_bundle()).applied,
              "SecurityPolicyManager contract rejection checks require a successfully loaded baseline snapshot");

  const auto patch = make_patch(manager.snapshot().generation);
  const auto dry_run_required_rejection = manager.apply_patch(patch);

  assert_true(!dry_run_required_rejection.applied &&
                  dry_run_required_rejection.result_code ==
                      map_policy_error_code(PolicyErrorCode::DryRunRejected).result_code &&
                  dry_run_required_rejection.references_only_contract_error_types(),
              "SecurityPolicyManager should keep dry-run rejections inside the frozen contracts policy failure domain");
}

void test_security_policy_manager_safe_mode_rejections_stay_in_policy_failure_domain() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);

  assert_true(manager.load_policy(make_bundle(2)).applied,
              "SecurityPolicyManager safe-mode rejection checks require a successfully loaded baseline snapshot");

  const auto patch = make_patch(manager.snapshot().generation);
  const auto first_rejection = manager.apply_patch(patch);
  const auto second_rejection = manager.apply_patch(patch);
  const auto safe_mode_rejection = manager.apply_patch(patch);

  assert_true(!first_rejection.applied && !second_rejection.applied && !safe_mode_rejection.applied &&
                  safe_mode_rejection.result_code ==
                      map_policy_error_code(PolicyErrorCode::DryRunRejected).result_code &&
                  safe_mode_rejection.references_only_contract_error_types() &&
                  safe_mode_rejection.error_info.has_value() &&
                  safe_mode_rejection.error_info->details.message.find("policy_safe_mode_active") !=
                      std::string::npos,
              "SecurityPolicyManager should keep safe-mode patch rejections inside the frozen contracts policy failure domain");
}

}  // namespace

int main() {
  try {
    test_security_policy_manager_rejections_stay_in_policy_failure_domain();
    test_security_policy_manager_safe_mode_rejections_stay_in_policy_failure_domain();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}