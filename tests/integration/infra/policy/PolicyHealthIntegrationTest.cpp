#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "policy/PolicyErrors.h"
#include "policy/PolicyHealthProbe.h"
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
                                                bool dry_run_required,
                                                std::uint32_t safe_mode_threshold) {
  return dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-health-integration-bundle-") + reason_code,
      .schema_version = std::string("1"),
      .source = std::string("source_id=defaults;version=defaults@resolved"),
      .checksum = std::string("sha256:policy-health-integration"),
      .rules = {
          make_patch_gate_rule(dry_run_required, safe_mode_threshold),
          make_default_rule(),
          make_plugin_rule(plugin_effect, std::move(reason_code)),
      },
      .generated_at = std::string("2026-04-05T23:20:00Z"),
  };
}

dasall::infra::policy::PolicyPatch make_replace_patch(std::uint64_t base_generation,
                                                      dasall::infra::policy::PolicyEffect effect,
                                                      std::string reason_code) {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("policy-health-integration-patch-001"),
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
      .reason = std::string("policy-health-integration-test"),
  };
}

class PolicyHealthIntegrationSignalProvider final
    : public dasall::infra::policy::IPolicyHealthSignalProvider {
 public:
  explicit PolicyHealthIntegrationSignalProvider(
      const dasall::infra::policy::PolicySnapshotStore& snapshot_store)
      : snapshot_store_(snapshot_store) {}

  void note_success() {
    safe_mode_ = false;
    consecutive_patch_failures_ = 0;
    last_policy_error_code_.reset();
    last_failure_reason_.clear();
  }

  void note_failure(dasall::infra::policy::PolicyErrorCode error_code,
                    std::string reason,
                    bool safe_mode = false) {
    ++consecutive_patch_failures_;
    safe_mode_ = safe_mode;
    last_policy_error_code_ = error_code;
    last_failure_reason_ = std::move(reason);
  }

  [[nodiscard]] dasall::infra::policy::PolicyHealthSample sample(
      std::int64_t) override {
    ++sample_count_;
    return dasall::infra::policy::PolicyHealthSample{
        .state = dasall::infra::policy::PolicyHealthSampleState::Ready,
        .signals = dasall::infra::policy::PolicyHealthSignals{
            .current_snapshot = snapshot_store_.current(),
            .last_known_good_snapshot = snapshot_store_.last_known_good(),
            .safe_mode = safe_mode_,
            .consecutive_patch_failures = consecutive_patch_failures_,
            .audit_bridge_degraded = false,
            .metrics_bridge_degraded = false,
            .last_policy_error_code = last_policy_error_code_,
            .last_failure_reason = last_failure_reason_,
        },
        .latency_ms = 2,
        .sampled_at_unix_ms = 1712140805000 +
                              static_cast<std::int64_t>(sample_count_ * 100),
    };
  }

 private:
  const dasall::infra::policy::PolicySnapshotStore& snapshot_store_;
  bool safe_mode_ = false;
  std::uint32_t consecutive_patch_failures_ = 0;
  std::optional<dasall::infra::policy::PolicyErrorCode> last_policy_error_code_;
  std::string last_failure_reason_;
  std::uint64_t sample_count_ = 0;
};

void test_policy_health_integration_degrades_after_patch_commit_failure() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);

  assert_true(manager
                  .load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                           "plugin_allowed",
                                           false,
                                           2))
                  .applied,
              "policy health integration should load a baseline snapshot before injecting commit failures");

  auto provider = std::make_shared<PolicyHealthIntegrationSignalProvider>(
      snapshot_store);
  provider->note_success();
  PolicyHealthProbe probe(provider);
  const auto healthy = probe.probe();

  assert_true(healthy.status == ProbeStatus::Healthy && healthy.has_consistent_state(),
              "policy health integration should start from a healthy readiness state after the first snapshot commits");

  const auto active_snapshot = snapshot_store.current();
  const auto patch = make_replace_patch(active_snapshot.generation,
                                        dasall::infra::policy::PolicyEffect::Deny,
                                        "plugin_denied_commit_fail");
  const auto dry_run = manager.dry_run_patch(patch);
  snapshot_store.inject_commit_failure_for_test("policy_store_commit_failed");
  const auto apply_result = manager.apply_patch(patch);
  provider->note_failure(PolicyErrorCode::StoreCommitFailed,
                         "policy_store_commit_failed");

  const auto degraded = probe.probe();

  assert_true(!dry_run.has_blocking_errors() && !apply_result.applied,
              "policy health integration should reproduce the commit-fail path before probing health");
  assert_true(degraded.status == ProbeStatus::Degraded &&
                  degraded.has_consistent_state() &&
                  !degraded.error_code.has_value(),
              "policy health integration should surface patch commit failures as degraded readiness facts rather than probe execution errors");
  assert_equal(
      std::string(
          "status://policy/health/degraded/recent_failure/INF_E_POLICY_STORE_COMMIT_FAILED/generation/1"),
      degraded.detail_ref,
      "policy health integration should keep the last healthy generation while exposing the frozen store-commit failure reason");
}

void test_policy_health_integration_surfaces_safe_mode_as_degraded() {
  using dasall::infra::ProbeStatus;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyHealthProbe;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::SecurityPolicyManager;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PolicySchemaValidator validator;
  PolicySnapshotStore snapshot_store;
  SecurityPolicyManager manager(validator, snapshot_store);

  assert_true(manager
                  .load_policy(make_bundle(dasall::infra::policy::PolicyEffect::Allow,
                                           "plugin_allowed",
                                           true,
                                           2))
                  .applied,
              "policy health integration should load a low-threshold policy before driving safe-mode failures");

  auto provider = std::make_shared<PolicyHealthIntegrationSignalProvider>(
      snapshot_store);
  provider->note_success();
  PolicyHealthProbe probe(provider);

  const auto active_snapshot = snapshot_store.current();
  const auto patch = make_replace_patch(active_snapshot.generation,
                                        dasall::infra::policy::PolicyEffect::Deny,
                                        "plugin_denied_safe_mode");
  const auto first_failure = manager.apply_patch(patch);
  provider->note_failure(PolicyErrorCode::DryRunRejected,
                         "policy_patch_dry_run_required");
  const auto second_failure = manager.apply_patch(patch);
  provider->note_failure(PolicyErrorCode::DryRunRejected,
                         "policy_patch_dry_run_required");
  const auto safe_mode_failure = manager.apply_patch(patch);
  provider->note_failure(PolicyErrorCode::DryRunRejected,
                         "policy_safe_mode_active",
                         true);

  const auto degraded = probe.probe();

  assert_true(!first_failure.applied && !second_failure.applied &&
                  !safe_mode_failure.applied,
              "policy health integration should reproduce the repeated patch failure path before checking safe-mode health");
  assert_true(degraded.status == ProbeStatus::Degraded &&
                  degraded.has_consistent_state() &&
                  !degraded.error_code.has_value(),
              "policy health integration should expose safe-mode readiness degradation as a health fact without synthesizing a probe execution error");
  assert_equal(std::string("status://policy/health/degraded/safe_mode/generation/1"),
               degraded.detail_ref,
               "policy health integration should prioritize safe_mode evidence while preserving the active snapshot generation fact");
}

}  // namespace

int main() {
  try {
    test_policy_health_integration_degrades_after_patch_commit_failure();
    test_policy_health_integration_surfaces_safe_mode_as_degraded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}