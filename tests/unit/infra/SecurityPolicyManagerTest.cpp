#include <exception>
#include <iostream>
#include <string>
#include <utility>

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
          std::string("policy_enabled=true"),
          std::string("hot_reload=true"),
          std::string("dry_run_required=") + (dry_run_required ? "true" : "false"),
          std::string("safe_mode_threshold=") + std::to_string(safe_mode_threshold),
          std::string("priority_order=deny-first"),
      },
      .reason_code = std::string("policy_patch_confirmation_required"),
  };
}

dasall::infra::policy::PolicyRuleDescriptor make_default_rule() {
  return dasall::infra::policy::PolicyRuleDescriptor{
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
  };
}

dasall::infra::policy::PolicyRuleDescriptor make_plugin_rule(dasall::infra::policy::PolicyEffect effect,
                                                             std::string reason_code) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("plugin-policy-rule"),
      .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
      .subject = std::string("ops-user"),
      .action = std::string("load"),
      .target_selector = std::string("manifest:plugin.echo"),
      .effect = effect,
      .priority = 5,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("priority_order=deny-first")},
      .reason_code = std::move(reason_code),
  };
}

dasall::infra::policy::PolicyBundle make_bundle(dasall::infra::policy::PolicyEffect plugin_effect,
                                                std::string reason_code,
                                                bool dry_run_required = true,
                                                std::uint32_t safe_mode_threshold = 3) {
  return dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-manager-bundle-") + std::move(reason_code),
      .schema_version = std::string("1"),
      .source = std::string("source_id=defaults;version=defaults@resolved"),
      .checksum = std::string("sha256:policy-manager"),
      .rules = {
          make_patch_gate_rule(dry_run_required, safe_mode_threshold),
          make_default_rule(),
          make_plugin_rule(plugin_effect, std::move(reason_code)),
      },
      .generated_at = std::string("2026-04-05T18:30:00Z"),
  };
}

dasall::infra::policy::PolicyPatch make_replace_patch(std::uint64_t base_generation,
                                                      dasall::infra::policy::PolicyEffect effect,
                                                      std::string reason_code) {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("patch-manager-001"),
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
              .effect = effect,
              .priority = 5,
              .mode = dasall::infra::policy::PolicyMode::Enforced,
              .conditions = {std::string("priority_order=deny-first")},
              .reason_code = std::move(reason_code),
          },
          .mode = dasall::infra::policy::PolicyMode::Unspecified,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("policy-manager-test"),
  };
}

dasall::infra::policy::PolicyQueryContext make_plugin_query() {
  return dasall::infra::policy::PolicyQueryContext{
      .module = std::string("plugin"),
      .operation = std::string("load"),
      .target_type = std::string("manifest"),
      .target_ref = std::string("plugin.echo"),
      .actor_ref = std::string("ops-user"),
  };
}

void test_security_policy_manager_loads_policy_and_projects_queries() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);

  const auto load_result = manager.load_policy(
      make_bundle(dasall::infra::policy::PolicyEffect::Allow, "plugin_allowed"));
  const auto active_snapshot = manager.snapshot();
  const auto decision = manager.evaluate(make_plugin_query());

  assert_true(load_result.applied && active_snapshot.is_valid() && active_snapshot.generation == 1 &&
                  decision.is_valid() && decision.decision == PolicyDecision::Allow &&
                  decision.reason_code == "plugin_allowed",
              "SecurityPolicyManager should load a valid bundle, commit the first snapshot, and project query decisions through the frozen manager boundary");
}

void test_security_policy_manager_rejects_patch_without_switching_current_snapshot() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);
  assert_true(manager.load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                              "plugin_allowed"))
                  .applied,
              "SecurityPolicyManager should accept the initial policy bundle before patch-failure checks");

  const auto before_snapshot = manager.snapshot();
  const auto failed_apply = manager.apply_patch(
      make_replace_patch(before_snapshot.generation,
                         dasall::infra::policy::PolicyEffect::Deny,
                         "plugin_denied"));
  const auto after_snapshot = manager.snapshot();

  assert_true(!failed_apply.applied &&
                  failed_apply.result_code ==
                      map_policy_error_code(PolicyErrorCode::DryRunRejected).result_code &&
                  before_snapshot.snapshot_id == after_snapshot.snapshot_id &&
                  before_snapshot.generation == after_snapshot.generation,
              "SecurityPolicyManager should reject patch application when dry-run is required and keep current snapshot stable on failure");
}

void test_security_policy_manager_rolls_back_to_a_historical_snapshot_after_successful_patch() {
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);

  assert_true(manager.load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                              "plugin_allowed"))
                  .applied,
              "SecurityPolicyManager should load the baseline snapshot before rollback tests");
  const auto first_snapshot = manager.snapshot();

  const auto patch = make_replace_patch(first_snapshot.generation,
                                        dasall::infra::policy::PolicyEffect::Deny,
                                        "plugin_denied");
  const auto dry_run_report = manager.dry_run_patch(patch);
  assert_true(!dry_run_report.has_blocking_errors(),
              "SecurityPolicyManager should accept a dry-run for a valid patch before applying it");

  const auto apply_result = manager.apply_patch(patch);
  assert_true(apply_result.applied,
              "SecurityPolicyManager should apply a dry-run-approved patch and commit a new snapshot");

  const auto denied_decision = manager.evaluate(make_plugin_query());
  assert_true(denied_decision.decision == PolicyDecision::Deny &&
                  denied_decision.reason_code == "plugin_denied",
              "SecurityPolicyManager should project the patched deny rule after a successful apply");

  const auto rollback_result = manager.rollback(first_snapshot.snapshot_id);
  const auto rolled_back_decision = manager.evaluate(make_plugin_query());
  const auto rolled_back_snapshot = manager.snapshot();

  assert_true(rollback_result.applied && rollback_result.rolled_back &&
                  rolled_back_snapshot.generation > apply_result.generation &&
                  rolled_back_decision.is_valid() &&
                  rolled_back_decision.decision == PolicyDecision::Allow &&
                  rolled_back_decision.reason_code == "plugin_allowed",
              "SecurityPolicyManager should restore historical policy semantics by committing a rollback snapshot instead of mutating history in place");
}

void test_security_policy_manager_enters_safe_mode_after_consecutive_patch_failures() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);
  assert_true(manager.load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                              "plugin_allowed",
                                              true,
                                              2))
                  .applied,
              "SecurityPolicyManager should load a bundle with a low safe-mode threshold before failure-threshold checks");

  const auto active_snapshot = manager.snapshot();
  const auto patch = make_replace_patch(active_snapshot.generation,
                                        dasall::infra::policy::PolicyEffect::Deny,
                                        "plugin_denied");

  const auto first_failure = manager.apply_patch(patch);
  const auto second_failure = manager.apply_patch(patch);
  const auto safe_mode_failure = manager.apply_patch(patch);

  assert_true(!first_failure.applied && !second_failure.applied && !safe_mode_failure.applied &&
                  safe_mode_failure.result_code ==
                      map_policy_error_code(PolicyErrorCode::DryRunRejected).result_code &&
                  safe_mode_failure.error_info.has_value() &&
                  safe_mode_failure.error_info->details.message.find("policy_safe_mode_active") !=
                      std::string::npos,
              "SecurityPolicyManager should enter safe mode after the configured number of consecutive patch failures and keep rejecting further patch attempts");
}

}  // namespace

int main() {
  try {
    test_security_policy_manager_loads_policy_and_projects_queries();
    test_security_policy_manager_rejects_patch_without_switching_current_snapshot();
    test_security_policy_manager_rolls_back_to_a_historical_snapshot_after_successful_patch();
    test_security_policy_manager_enters_safe_mode_after_consecutive_patch_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}