#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "policy/PolicyConflictResolver.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id,
                                                      dasall::infra::policy::PolicyEffect effect,
                                                      std::uint32_t priority,
                                                      dasall::infra::policy::PolicyMode mode,
                                                      std::string priority_order = "deny-first") {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("infra.policy"),
      .action = std::string("apply_patch"),
      .target_selector = std::string("policy:runtime_patch"),
      .effect = effect,
      .priority = priority,
      .mode = mode,
      .conditions = {std::string("priority_order=") + std::move(priority_order)},
      .reason_code = std::string("policy_conflict_test_guard"),
  };
}

dasall::infra::policy::PolicyBundle make_bundle(
    std::vector<dasall::infra::policy::PolicyRuleDescriptor> rules) {
  return dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-conflict-bundle-001"),
      .schema_version = std::string("1"),
      .source = std::string("source_id=infra/config/defaults/runtime_policy.yaml;version=defaults@1"),
      .checksum = std::string("sha256:policy-conflict-001"),
      .rules = std::move(rules),
      .generated_at = std::string("2026-04-05T14:10:00Z"),
  };
}

void test_policy_conflict_resolver_prefers_deny_first_rules() {
  using dasall::infra::policy::PolicyConflictResolver;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyMode;
  using dasall::tests::support::assert_true;

  const PolicyConflictResolver resolver;
  const auto result = resolver.resolve(make_bundle({
      make_rule("allow-rule", PolicyEffect::Allow, 1, PolicyMode::Enforced, "deny-first"),
      make_rule("deny-rule", PolicyEffect::Deny, 50, PolicyMode::Enforced, "deny-first"),
  }));

  assert_true(result.resolved && result.priority_order == "deny-first" &&
                  result.effective_rules.size() == 1 &&
                  result.effective_rules.front().rule_id == "deny-rule",
              "PolicyConflictResolver should let deny-first precedence override a numerically higher-priority allow rule");
}

void test_policy_conflict_resolver_prefers_explicit_priority_rules() {
  using dasall::infra::policy::PolicyConflictResolver;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyMode;
  using dasall::tests::support::assert_true;

  const PolicyConflictResolver resolver;
  const auto result = resolver.resolve(make_bundle({
      make_rule("allow-rule", PolicyEffect::Allow, 1, PolicyMode::Enforced, "explicit-priority"),
      make_rule("deny-rule", PolicyEffect::Deny, 5, PolicyMode::Enforced, "explicit-priority"),
  }));

  assert_true(result.resolved && result.priority_order == "explicit-priority" &&
                  result.effective_rules.size() == 1 &&
                  result.effective_rules.front().rule_id == "allow-rule",
              "PolicyConflictResolver should let explicit-priority order favor the numerically strongest rule before effect precedence");
}

void test_policy_conflict_resolver_rejects_unresolved_ties_and_downgrades_compat_groups() {
  using dasall::infra::policy::PolicyConflictResolver;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyMode;
  using dasall::tests::support::assert_true;

  const PolicyConflictResolver resolver;

  const auto unresolved = resolver.resolve(make_bundle({
      make_rule("allow-tie-a", PolicyEffect::Allow, 7, PolicyMode::Enforced, "explicit-priority"),
      make_rule("allow-tie-b", PolicyEffect::Allow, 7, PolicyMode::Enforced, "explicit-priority"),
  }));
  assert_true(!unresolved.resolved && unresolved.reason_code == "policy_conflict_unresolved" &&
                  unresolved.conflict_rule_ids.size() == 2,
              "PolicyConflictResolver should reject activation when enforced rules remain tied after the frozen precedence steps");

  const auto compat_downgraded = resolver.resolve(make_bundle({
      make_rule("compat-allow-a", PolicyEffect::Allow, 7, PolicyMode::Compatibility, "explicit-priority"),
      make_rule("compat-allow-b", PolicyEffect::Allow, 7, PolicyMode::Compatibility, "explicit-priority"),
  }));
  assert_true(compat_downgraded.resolved && !compat_downgraded.warnings.empty() &&
                  compat_downgraded.effective_rules.size() == 1,
              "PolicyConflictResolver should degrade same-rank compatibility-only ties into a warning instead of rejecting activation");
}

}  // namespace

int main() {
  try {
    test_policy_conflict_resolver_prefers_deny_first_rules();
    test_policy_conflict_resolver_prefers_explicit_priority_rules();
    test_policy_conflict_resolver_rejects_unresolved_ties_and_downgrades_compat_groups();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}