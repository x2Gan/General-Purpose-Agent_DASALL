#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "policy/ISecurityPolicyManager.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasLoadFromSourcesMethod = requires {
  &T::load_from_sources;
};

template <typename T>
concept HasValidateBundleMethod = requires {
  &T::validate_bundle;
};

template <typename T>
concept HasCommitMethod = requires {
  &T::commit;
};

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id,
                                                      std::string reason_code) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::PluginLoad,
      .subject = std::string("plugin"),
      .action = std::string("load"),
      .target_selector = std::string("plugin:diagnostics-export"),
      .effect = dasall::infra::policy::PolicyEffect::RequireConfirmation,
      .priority = 90,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("signed")},
      .reason_code = std::move(reason_code),
  };
}

class StaticSecurityPolicyManager final : public dasall::infra::policy::ISecurityPolicyManager {
 public:
  StaticSecurityPolicyManager()
      : snapshot_{
            .snapshot_id = std::string("snapshot-007"),
            .generation = 7,
            .version = std::string("policy-v7"),
            .mode = dasall::infra::policy::PolicyMode::Enforced,
            .effective_rules = {make_rule("require-plugin-confirmation",
                                          "plugin_confirmation_required")},
            .created_at = std::string("2026-04-01T12:00:00Z"),
            .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
            .last_known_good_ref = std::string("snapshot-006"),
        } {}

  [[nodiscard]] dasall::infra::policy::PolicyOpResult load_policy(
      const dasall::infra::policy::PolicyBundle& bundle) override {
    last_loaded_bundle_id_ = bundle.bundle_id;
    return dasall::infra::policy::PolicyOpResult::success(snapshot_.snapshot_id,
                                                          snapshot_.generation);
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult apply_patch(
      const dasall::infra::policy::PolicyPatch& patch) override {
    last_patch_id_ = patch.patch_id;
    return dasall::infra::policy::PolicyOpResult::success(snapshot_.snapshot_id,
                                                          patch.base_generation + 1);
  }

  [[nodiscard]] dasall::infra::policy::ValidationReport dry_run_patch(
      const dasall::infra::policy::PolicyPatch& patch) override {
    if (!patch.is_valid()) {
      return dasall::infra::policy::ValidationReport{
          .blocking_errors = {std::string("invalid patch")},
          .warnings = {},
          .invalid_rule_ids = {std::string("missing-rule")},
          .field_paths = {std::string("operations[0].rule")},
      };
    }

    return dasall::infra::policy::ValidationReport{};
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot snapshot() const override {
    return snapshot_;
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult rollback(
      const std::string& snapshot_id) override {
    if (snapshot_id.empty()) {
      return dasall::infra::policy::PolicyOpResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string("snapshot id is required"),
          std::string("policy.rollback"),
          std::string("StaticSecurityPolicyManager"));
    }

    return dasall::infra::policy::PolicyOpResult::success(snapshot_id,
                                                          snapshot_.generation,
                                                          true);
  }

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::policy::PolicyQueryContext& query) const override {
    return dasall::infra::policy::PolicyDecisionRef{
        .decision = dasall::infra::policy::PolicyDecision::RequireConfirmation,
        .reason_code = std::string("plugin_confirmation_required"),
        .matched_rule_ids = {std::string("require-plugin-confirmation")},
        .snapshot_id = snapshot_.snapshot_id,
        .generation = snapshot_.generation,
        .evidence_ref = std::string("audit:policy/decision/007"),
        .warnings = {query.profile_id == "unknown" ? std::string("profile_unknown")
                                                    : std::string("compatibility_mode")},
    };
  }

  [[nodiscard]] const std::string& last_loaded_bundle_id() const {
    return last_loaded_bundle_id_;
  }

  [[nodiscard]] const std::string& last_patch_id() const {
    return last_patch_id_;
  }

 private:
  dasall::infra::policy::PolicySnapshot snapshot_;
  std::string last_loaded_bundle_id_;
  std::string last_patch_id_;
};

void test_security_policy_manager_interface_keeps_six_frozen_entrypoints() {
  using dasall::infra::policy::ISecurityPolicyManager;
  using dasall::infra::policy::PolicyBundle;
  using dasall::infra::policy::PolicyDecision;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::infra::policy::PolicyOpResult;
  using dasall::infra::policy::PolicyPatch;
  using dasall::infra::policy::PolicyPatchOperation;
  using dasall::infra::policy::PolicyPatchOperationType;
  using dasall::infra::policy::PolicyQueryContext;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::infra::policy::ValidationReport;
  using dasall::tests::support::assert_true;

  using LoadPolicySignature = PolicyOpResult (ISecurityPolicyManager::*)(const PolicyBundle&);
  using ApplyPatchSignature = PolicyOpResult (ISecurityPolicyManager::*)(const PolicyPatch&);
  using DryRunPatchSignature = ValidationReport (ISecurityPolicyManager::*)(const PolicyPatch&);
  using SnapshotSignature = PolicySnapshot (ISecurityPolicyManager::*)() const;
  using RollbackSignature = PolicyOpResult (ISecurityPolicyManager::*)(const std::string&);
  using EvaluateSignature = PolicyDecisionRef (ISecurityPolicyManager::*)(const PolicyQueryContext&) const;

  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::load_policy), LoadPolicySignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::apply_patch), ApplyPatchSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::dry_run_patch), DryRunPatchSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::snapshot), SnapshotSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::rollback), RollbackSignature>);
  static_assert(std::is_same_v<decltype(&ISecurityPolicyManager::evaluate), EvaluateSignature>);
  static_assert(std::is_abstract_v<ISecurityPolicyManager>);

  StaticSecurityPolicyManager manager;

  const PolicyBundle bundle{
      .bundle_id = std::string("bundle-007"),
      .schema_version = std::string("v1"),
      .source = std::string("profile:desktop_full"),
      .checksum = std::string("sha256:bundle-007"),
      .rules = {make_rule("require-plugin-confirmation", "plugin_confirmation_required")},
      .generated_at = std::string("2026-04-01T11:59:00Z"),
  };

  const auto load_result = manager.load_policy(bundle);
  assert_true(load_result.applied && manager.last_loaded_bundle_id() == "bundle-007",
              "ISecurityPolicyManager should keep load_policy as the frozen bundle-based entrypoint");

  const PolicyPatch patch{
      .patch_id = std::string("patch-007"),
      .base_generation = 7,
      .operations = {PolicyPatchOperation{
          .operation = PolicyPatchOperationType::UpdateMode,
          .rule_id = std::string(),
          .rule = std::nullopt,
          .mode = dasall::infra::policy::PolicyMode::Compatibility,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("temporary maintenance window"),
  };

  const auto dry_run_report = manager.dry_run_patch(patch);
  assert_true(!dry_run_report.has_blocking_errors(),
              "ISecurityPolicyManager should keep dry_run_patch returning the frozen ValidationReport boundary");

  const auto apply_result = manager.apply_patch(patch);
  assert_true(apply_result.applied && manager.last_patch_id() == "patch-007",
              "ISecurityPolicyManager should keep apply_patch returning the frozen operation result boundary");

  const auto snapshot = manager.snapshot();
  assert_true(snapshot.is_valid() && snapshot.can_roll_back(),
              "ISecurityPolicyManager should expose snapshot retrieval without leaking store internals");

  const PolicyQueryContext query{
      .module = std::string("plugin"),
      .operation = std::string("load"),
      .target_type = std::string("plugin"),
      .target_ref = std::string("diagnostics-export"),
      .actor_ref = std::string("ops-user"),
  };

  const auto decision = manager.evaluate(query);
  assert_true(decision.is_valid() && decision.decision == PolicyDecision::RequireConfirmation,
              "ISecurityPolicyManager should expose only the frozen query and decision references on evaluate");

  const auto rollback_result = manager.rollback("snapshot-006");
  assert_true(rollback_result.applied && rollback_result.rolled_back,
              "ISecurityPolicyManager should keep rollback as a snapshot-id based operation result boundary");
}

void test_security_policy_manager_interface_does_not_absorb_loader_validator_or_store_methods() {
  using dasall::infra::policy::ISecurityPolicyManager;
  using dasall::tests::support::assert_true;

  static_assert(!HasLoadFromSourcesMethod<ISecurityPolicyManager>);
  static_assert(!HasValidateBundleMethod<ISecurityPolicyManager>);
  static_assert(!HasCommitMethod<ISecurityPolicyManager>);

  assert_true(std::has_virtual_destructor_v<ISecurityPolicyManager>,
              "ISecurityPolicyManager should keep a virtual destructor as the only lifecycle requirement of the pure abstract boundary");
  assert_true(!std::is_default_constructible_v<ISecurityPolicyManager>,
              "ISecurityPolicyManager should remain abstract and should not collapse loader, validator, or store responsibilities into the manager boundary");
}

}  // namespace

int main() {
  try {
    test_security_policy_manager_interface_keeps_six_frozen_entrypoints();
    test_security_policy_manager_interface_does_not_absorb_loader_validator_or_store_methods();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}