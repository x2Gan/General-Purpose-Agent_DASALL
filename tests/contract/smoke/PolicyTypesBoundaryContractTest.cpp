#include <exception>
#include <iostream>
#include <string_view>

#include "policy/PolicyTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_policy_domain_names_stay_on_infra_private_semantic_mapping() {
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::to_string;
  using dasall::tests::support::assert_true;

  assert_true(to_string(PolicyDomain::SecretAccess) == std::string_view("secret_access"),
              "policy domain mapping should keep the frozen secret_access semantic name");
  assert_true(to_string(PolicyDomain::PluginLoad) == std::string_view("plugin_load"),
              "policy domain mapping should keep the frozen plugin_load semantic name");
  assert_true(to_string(PolicyDomain::DiagnosticsCommand) ==
                  std::string_view("diagnostics_command"),
              "policy domain mapping should keep the frozen diagnostics_command semantic name");
  assert_true(to_string(PolicyDomain::OTAApply) == std::string_view("ota_apply"),
              "policy domain mapping should keep the frozen ota_apply semantic name");
  assert_true(to_string(PolicyDomain::OTARollback) == std::string_view("ota_rollback"),
              "policy domain mapping should keep the frozen ota_rollback semantic name");
  assert_true(to_string(PolicyDomain::PolicyAdmin) == std::string_view("policy_admin"),
              "policy domain mapping should keep the frozen policy_admin semantic name");
}

void test_policy_effect_names_keep_deny_first_semantics_without_expanding_contracts() {
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::policy_effect_precedence;
  using dasall::infra::policy::to_string;
  using dasall::tests::support::assert_true;

  assert_true(to_string(PolicyEffect::Allow) == std::string_view("allow"),
              "policy effect mapping should keep the frozen allow semantic name");
  assert_true(to_string(PolicyEffect::Deny) == std::string_view("deny"),
              "policy effect mapping should keep the frozen deny semantic name");
  assert_true(to_string(PolicyEffect::RequireConfirmation) ==
                  std::string_view("require_confirmation"),
              "policy effect mapping should keep the frozen require_confirmation semantic name");

  assert_true(policy_effect_precedence(PolicyEffect::Deny) <
                  policy_effect_precedence(PolicyEffect::RequireConfirmation),
              "deny should stay ahead of require_confirmation in infra-private effect precedence");
  assert_true(policy_effect_precedence(PolicyEffect::RequireConfirmation) <
                  policy_effect_precedence(PolicyEffect::Allow),
              "require_confirmation should stay ahead of allow in infra-private effect precedence");
}

}  // namespace

int main() {
  try {
    test_policy_domain_names_stay_on_infra_private_semantic_mapping();
    test_policy_effect_names_keep_deny_first_semantics_without_expanding_contracts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
