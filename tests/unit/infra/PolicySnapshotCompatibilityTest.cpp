#include <exception>
#include <iostream>
#include <string>

#include "policy/ISecurityPolicyManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id,
                                                      dasall::infra::policy::PolicyEffect effect,
                                                      std::uint32_t priority) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::DiagnosticsCommand,
      .subject = std::string("ops"),
      .action = std::string("execute"),
      .target_selector = std::string("diag.export"),
      .effect = effect,
      .priority = priority,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {"profile=edge_balanced"},
      .reason_code = std::string("diag_policy_guard"),
  };
}

void test_policy_snapshot_preserves_generation_and_lkg_roll_back_contract() {
  using dasall::infra::policy::PolicyBundle;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyMode;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::tests::support::assert_true;

  const PolicyBundle bundle{
      .bundle_id = std::string("bundle-001"),
      .schema_version = std::string("v1"),
      .source = std::string("profile:edge_balanced"),
      .checksum = std::string("sha256:001"),
      .rules = {make_rule("deny-diag-export", PolicyEffect::Deny, 0),
                make_rule("allow-diag-export", PolicyEffect::Allow, 10)},
      .generated_at = std::string("2026-03-27T09:00:00Z"),
  };

  assert_true(bundle.is_valid(),
              "policy bundle should stay valid once minimal schema fields are frozen");
  assert_true(dasall::infra::policy::policy_effect_precedence(PolicyEffect::Deny) <
                  dasall::infra::policy::policy_effect_precedence(PolicyEffect::Allow),
              "deny should remain higher precedence than allow");

  const PolicySnapshot snapshot{
      .snapshot_id = std::string("snapshot-002"),
      .generation = 2,
      .version = std::string("2026.03.27"),
      .mode = PolicyMode::Enforced,
      .effective_rules = bundle.rules,
      .created_at = std::string("2026-03-27T09:01:00Z"),
      .source_chain = {"defaults", "profile", "deployment_override"},
      .last_known_good_ref = std::string("snapshot-001"),
  };

  assert_true(snapshot.is_valid(),
              "policy snapshot should remain valid when generation, source_chain, and rules are present");
  assert_true(snapshot.can_roll_back(),
              "policy snapshot should advertise rollback readiness when last-known-good is present");
}

void test_policy_patch_rejects_non_whitelisted_operations() {
  using dasall::infra::policy::PolicyPatch;
  using dasall::infra::policy::PolicyPatchOperation;
  using dasall::infra::policy::PolicyPatchOperationType;
  using dasall::tests::support::assert_true;

  const PolicyPatch invalid_patch{
      .patch_id = std::string("patch-001"),
      .base_generation = 2,
      .operations = {PolicyPatchOperation{.operation = PolicyPatchOperationType::UpdateMode,
                        .rule_id = std::string("deny-diag-export"),
                        .rule = std::nullopt,
                        .mode = dasall::infra::policy::PolicyMode::Unspecified}},
      .actor = std::string("ops-user"),
      .reason = std::string("maintenance-window"),
  };

  assert_true(!invalid_patch.is_valid(),
              "policy patch should reject operations that do not satisfy the frozen white-list shape");
}

}  // namespace

int main() {
  try {
    test_policy_snapshot_preserves_generation_and_lkg_roll_back_contract();
    test_policy_patch_rejects_non_whitelisted_operations();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}