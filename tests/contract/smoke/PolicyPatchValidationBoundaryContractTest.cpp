#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "policy/PolicyTypes.h"
#include "support/TestAssertions.h"

namespace {

void test_policy_patch_validation_boundary_keeps_local_string_reports() {
  using dasall::infra::policy::PolicyPatchOperationType;
  using dasall::infra::policy::ValidationReport;
  using dasall::infra::policy::to_string;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ValidationReport{}.blocking_errors),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ValidationReport{}.warnings), std::vector<std::string>>);
  static_assert(
      std::is_same_v<decltype(ValidationReport{}.invalid_rule_ids), std::vector<std::string>>);

  assert_true(to_string(PolicyPatchOperationType::AddRule) == std::string_view("add_rule"),
              "policy patch operation mapping should keep the frozen add_rule semantic name");
  assert_true(to_string(PolicyPatchOperationType::ReplaceRule) ==
                  std::string_view("replace_rule"),
              "policy patch operation mapping should keep the frozen replace_rule semantic name");
  assert_true(to_string(PolicyPatchOperationType::RemoveRule) ==
                  std::string_view("remove_rule"),
              "policy patch operation mapping should keep the frozen remove_rule semantic name");
  assert_true(to_string(PolicyPatchOperationType::UpdateMode) ==
                  std::string_view("update_mode"),
              "policy patch operation mapping should keep the frozen update_mode semantic name");

  const ValidationReport report{
      .blocking_errors = {std::string("patch_base_mismatch")},
      .warnings = {std::string("dry_run_required")},
      .invalid_rule_ids = {std::string("rule-001")},
      .field_paths = {std::string("base_generation")},
  };

  assert_true(report.has_blocking_errors(),
              "policy validation reports should remain locally blocking without introducing contracts shared error objects");
}

void test_policy_patch_boundary_rejects_invalid_operation_shapes() {
  using dasall::infra::policy::PolicyPatchOperation;
  using dasall::infra::policy::PolicyPatchOperationType;
  using dasall::tests::support::assert_true;

  const PolicyPatchOperation invalid_remove{
      .operation = PolicyPatchOperationType::RemoveRule,
      .rule_id = {},
      .rule = std::nullopt,
      .mode = dasall::infra::policy::PolicyMode::Unspecified,
  };

  assert_true(!invalid_remove.is_valid(),
              "policy patch boundary should reject remove_rule operations that omit the frozen rule_id field");
}

}  // namespace

int main() {
  try {
    test_policy_patch_validation_boundary_keeps_local_string_reports();
    test_policy_patch_boundary_rejects_invalid_operation_shapes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
