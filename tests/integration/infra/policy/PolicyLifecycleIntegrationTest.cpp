#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "policy/PolicyErrors.h"
#include "policy/PolicySchemaValidator.h"
#include "policy/PolicySnapshotStore.h"
#include "policy/SecurityPolicyManager.h"
#include "dasall/tests/support/TestAssertions.h"

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
      .bundle_id = std::string("policy-integration-bundle-") + reason_code,
      .schema_version = std::string("1"),
      .source = std::string("source_id=defaults;version=defaults@resolved"),
      .checksum = std::string("sha256:policy-integration"),
      .rules = {
          make_patch_gate_rule(dry_run_required, safe_mode_threshold),
          make_default_rule(),
          make_plugin_rule(plugin_effect, std::move(reason_code)),
      },
      .generated_at = std::string("2026-04-05T22:10:00Z"),
  };
}

dasall::infra::policy::PolicyPatch make_replace_patch(std::uint64_t base_generation,
                                                      dasall::infra::policy::PolicyEffect effect,
                                                      std::string reason_code) {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("policy-integration-patch-001"),
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
      .reason = std::string("policy-integration-test"),
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

void test_policy_lifecycle_integration_closes_load_patch_rollback_loop() {
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
  const auto first_snapshot = manager.snapshot();
  const auto allowed_decision = manager.evaluate(make_plugin_query());

  assert_true(load_result.applied && first_snapshot.is_valid() && first_snapshot.generation == 1 &&
                  allowed_decision.is_valid() &&
                  allowed_decision.decision == PolicyDecision::Allow &&
                  allowed_decision.reason_code == "plugin_allowed",
              "Policy lifecycle integration should load the baseline bundle and expose the first projected allow decision");

  const auto patch = make_replace_patch(first_snapshot.generation,
                                        dasall::infra::policy::PolicyEffect::Deny,
                                        "plugin_denied");
  const auto dry_run_report = manager.dry_run_patch(patch);
  const auto apply_result = manager.apply_patch(patch);
  const auto denied_decision = manager.evaluate(make_plugin_query());
  const auto second_snapshot = manager.snapshot();

  assert_true(!dry_run_report.has_blocking_errors() && apply_result.applied &&
                  second_snapshot.is_valid() && second_snapshot.generation == 2 &&
                  denied_decision.is_valid() && denied_decision.decision == PolicyDecision::Deny &&
                  denied_decision.reason_code == "plugin_denied",
              "Policy lifecycle integration should dry-run and apply a patch, then project the patched deny decision from the new snapshot");

  const auto rollback_result = manager.rollback(first_snapshot.snapshot_id);
  const auto rolled_back_snapshot = manager.snapshot();
  const auto rolled_back_decision = manager.evaluate(make_plugin_query());

  assert_true(rollback_result.applied && rollback_result.rolled_back &&
                  rolled_back_snapshot.is_valid() && rolled_back_snapshot.generation == 3 &&
                  rolled_back_decision.is_valid() &&
                  rolled_back_decision.decision == PolicyDecision::Allow &&
                  rolled_back_decision.reason_code == "plugin_allowed",
              "Policy lifecycle integration should restore the historical allow semantics by committing a rollback snapshot instead of mutating history");
}

void test_policy_lifecycle_integration_surfaces_commit_failures_and_safe_mode() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore failing_snapshot_store;
  SecurityPolicyManager failing_manager(validator, failing_snapshot_store);

  assert_true(failing_manager
                  .load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                           "plugin_allowed",
                                           false,
                                           2))
                  .applied,
              "Policy lifecycle integration should load the baseline snapshot before injecting store commit failures");
  const auto before_failure_snapshot = failing_manager.snapshot();
  const auto failing_patch = make_replace_patch(before_failure_snapshot.generation,
                                                dasall::infra::policy::PolicyEffect::Deny,
                                                "plugin_denied_commit_fail");
  const auto failing_dry_run = failing_manager.dry_run_patch(failing_patch);
  failing_snapshot_store.inject_commit_failure_for_test("policy_store_commit_failed");
  const auto commit_failure = failing_manager.apply_patch(failing_patch);
  const auto after_failure_snapshot = failing_manager.snapshot();

  assert_true(!failing_dry_run.has_blocking_errors() && !commit_failure.applied &&
                  commit_failure.result_code ==
                      map_policy_error_code(PolicyErrorCode::StoreCommitFailed).result_code &&
                  after_failure_snapshot.snapshot_id == before_failure_snapshot.snapshot_id &&
                  after_failure_snapshot.generation == before_failure_snapshot.generation,
              "Policy lifecycle integration should keep the current snapshot stable when patch commit fails after a successful dry-run");

  PolicySnapshotStore safe_mode_snapshot_store;
  SecurityPolicyManager safe_mode_manager(validator, safe_mode_snapshot_store);
  assert_true(safe_mode_manager
                  .load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                           "plugin_allowed",
                                           true,
                                           2))
                  .applied,
              "Policy lifecycle integration should load a bundle with a low safe-mode threshold before verifying fail-fast patch gating");

  const auto active_snapshot = safe_mode_manager.snapshot();
  const auto safe_mode_patch = make_replace_patch(active_snapshot.generation,
                                                  dasall::infra::policy::PolicyEffect::Deny,
                                                  "plugin_denied_safe_mode");
  const auto first_failure = safe_mode_manager.apply_patch(safe_mode_patch);
  const auto second_failure = safe_mode_manager.apply_patch(safe_mode_patch);
  const auto safe_mode_failure = safe_mode_manager.apply_patch(safe_mode_patch);

  assert_true(!first_failure.applied && !second_failure.applied && !safe_mode_failure.applied &&
                  safe_mode_failure.result_code ==
                      map_policy_error_code(PolicyErrorCode::DryRunRejected).result_code &&
                  safe_mode_failure.error_info.has_value() &&
                  safe_mode_failure.error_info->details.message.find("policy_safe_mode_active") !=
                      std::string::npos,
              "Policy lifecycle integration should enter safe mode after repeated patch failures and then reject subsequent apply_patch calls fail-fast");
}

}  // namespace

int main() {
  try {
    test_policy_lifecycle_integration_closes_load_patch_rollback_loop();
    test_policy_lifecycle_integration_surfaces_commit_failures_and_safe_mode();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}