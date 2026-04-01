#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "policy/PolicyTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("ops"),
      .action = std::string("patch"),
      .target_selector = std::string("policy.current"),
      .effect = dasall::infra::policy::PolicyEffect::Deny,
      .priority = 1,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("actor=ops-user")},
      .reason_code = std::string("policy_patch_guard"),
  };
}

void test_policy_patch_freezes_required_metadata_and_operation_whitelist() {
  using dasall::infra::policy::PolicyPatch;
  using dasall::infra::policy::PolicyPatchOperation;
  using dasall::infra::policy::PolicyPatchOperationType;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyPatch{}.patch_id), std::string>);
  static_assert(std::is_same_v<decltype(PolicyPatch{}.base_generation), std::uint64_t>);
  static_assert(std::is_same_v<decltype(PolicyPatch{}.operations),
                               std::vector<PolicyPatchOperation>>);

  const PolicyPatch valid_patch{
      .patch_id = std::string("patch-001"),
      .base_generation = 7,
      .operations = {PolicyPatchOperation{
          .operation = PolicyPatchOperationType::AddRule,
          .rule_id = {},
          .rule = make_rule("rule-added-001"),
          .mode = dasall::infra::policy::PolicyMode::Unspecified,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("maintenance-window"),
  };

  assert_true(valid_patch.is_valid(),
              "policy patch should accept a frozen metadata envelope with whitelisted add_rule operations");

  PolicyPatch invalid_patch = valid_patch;
  invalid_patch.operations = {PolicyPatchOperation{
      .operation = PolicyPatchOperationType::UpdateMode,
      .rule_id = std::string("rule-added-001"),
      .rule = std::nullopt,
      .mode = dasall::infra::policy::PolicyMode::Unspecified,
  }};
  assert_true(!invalid_patch.is_valid(),
              "policy patch should reject operations outside the frozen update_mode shape");
}

void test_validation_report_keeps_blocking_and_warning_channels_separate() {
  using dasall::infra::policy::ValidationReport;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ValidationReport{}.blocking_errors),
                               std::vector<std::string>>);
  static_assert(
      std::is_same_v<decltype(ValidationReport{}.field_paths), std::vector<std::string>>);

  const ValidationReport clean_report{
      .blocking_errors = {},
      .warnings = {std::string("compat_mode_downgraded")},
      .invalid_rule_ids = {},
      .field_paths = {},
  };
  assert_true(!clean_report.has_blocking_errors(),
              "validation report should keep warnings non-blocking when no blocking_errors are present");

  const ValidationReport blocking_report{
      .blocking_errors = {std::string("patch_base_mismatch")},
      .warnings = {std::string("compat_mode_downgraded")},
      .invalid_rule_ids = {std::string("rule-added-001")},
      .field_paths = {std::string("base_generation")},
  };
  assert_true(blocking_report.has_blocking_errors(),
              "validation report should become blocking when blocking_errors is populated");
}

}  // namespace

int main() {
  try {
    test_policy_patch_freezes_required_metadata_and_operation_whitelist();
    test_validation_report_keeps_blocking_and_warning_channels_separate();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
