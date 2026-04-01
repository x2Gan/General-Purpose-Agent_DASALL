#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "policy/PolicyTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule() {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("diag-export-guard"),
      .domain = dasall::infra::policy::PolicyDomain::DiagnosticsCommand,
      .subject = std::string("ops"),
      .action = std::string("execute"),
      .target_selector = std::string("diag.export"),
      .effect = dasall::infra::policy::PolicyEffect::Deny,
      .priority = 0,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("profile=edge_balanced")},
      .reason_code = std::string("diag_export_denied"),
  };
}

void test_policy_rule_descriptor_freezes_required_fields() {
  using dasall::infra::policy::PolicyRuleDescriptor;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyRuleDescriptor{}.rule_id), std::string>);
  static_assert(std::is_same_v<decltype(PolicyRuleDescriptor{}.priority), std::uint32_t>);
  static_assert(
      std::is_same_v<decltype(PolicyRuleDescriptor{}.conditions), std::vector<std::string>>);

  const PolicyRuleDescriptor valid_rule = make_rule();
  assert_true(valid_rule.is_valid(),
              "policy rule descriptor should accept the frozen id, domain, effect, priority, mode, conditions, and reason fields");

  PolicyRuleDescriptor missing_reason = valid_rule;
  missing_reason.reason_code.clear();
  assert_true(!missing_reason.is_valid(),
              "policy rule descriptor should reject rules that omit the frozen reason_code field");
}

void test_policy_bundle_freezes_identity_and_rule_collection_fields() {
  using dasall::infra::policy::PolicyBundle;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyBundle{}.bundle_id), std::string>);
  static_assert(std::is_same_v<decltype(PolicyBundle{}.rules),
                               std::vector<dasall::infra::policy::PolicyRuleDescriptor>>);

  const PolicyBundle valid_bundle{
      .bundle_id = std::string("bundle-001"),
      .schema_version = std::string("v1"),
      .source = std::string("profile:edge_balanced"),
      .checksum = std::string("sha256:001"),
      .rules = {make_rule()},
      .generated_at = std::string("2026-04-01T09:00:00Z"),
  };

  assert_true(valid_bundle.is_valid(),
              "policy bundle should accept the frozen bundle identity, checksum, and rules collection fields");

  PolicyBundle missing_checksum = valid_bundle;
  missing_checksum.checksum.clear();
  assert_true(!missing_checksum.is_valid(),
              "policy bundle should reject bundles that omit the frozen checksum field");
}

}  // namespace

int main() {
  try {
    test_policy_rule_descriptor_freezes_required_fields();
    test_policy_bundle_freezes_identity_and_rule_collection_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
