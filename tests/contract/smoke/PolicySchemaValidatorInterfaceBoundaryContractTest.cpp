#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "policy/IPolicySchemaValidator.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasResultCodeField = requires {
  &T::result_code;
};

template <typename T>
concept HasErrorInfoField = requires {
  &T::error_info;
};

void test_policy_schema_validator_interface_keeps_validation_report_local_to_policy() {
  using dasall::infra::policy::IPolicySchemaValidator;
  using dasall::infra::policy::PolicyBundle;
  using dasall::infra::policy::PolicyPatch;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::infra::policy::ValidationReport;
  using dasall::tests::support::assert_true;

  using ValidateBundleSignature = ValidationReport (IPolicySchemaValidator::*)(const PolicyBundle&) const;
  using ValidatePatchSignature =
      ValidationReport (IPolicySchemaValidator::*)(const PolicySnapshot&, const PolicyPatch&) const;

  static_assert(std::is_same_v<decltype(&IPolicySchemaValidator::validate_bundle),
                               ValidateBundleSignature>);
  static_assert(std::is_same_v<decltype(&IPolicySchemaValidator::validate_patch),
                               ValidatePatchSignature>);
  static_assert(std::is_same_v<decltype(ValidationReport{}.blocking_errors), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ValidationReport{}.warnings), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ValidationReport{}.invalid_rule_ids),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ValidationReport{}.field_paths), std::vector<std::string>>);

  assert_true(std::is_abstract_v<IPolicySchemaValidator>,
              "IPolicySchemaValidator should remain a pure abstract validation boundary");
  assert_true(std::has_virtual_destructor_v<IPolicySchemaValidator>,
              "IPolicySchemaValidator should keep a virtual destructor for polymorphic consumers");
  assert_true(!HasResultCodeField<ValidationReport> && !HasErrorInfoField<ValidationReport>,
              "ValidationReport should stay local to policy and must not grow contracts ResultCode/ErrorInfo fields across the boundary");

  const ValidationReport report{
      .blocking_errors = {std::string("policy_schema_unsupported")},
      .warnings = {std::string("compat_mode_ignored")},
      .invalid_rule_ids = {std::string("rule-unsupported-001")},
      .field_paths = {std::string("rules[0].domain")},
  };

  assert_true(report.has_blocking_errors(),
              "policy schema validator boundary should keep blocking_errors inside the local ValidationReport string lists");
}

}  // namespace

int main() {
  try {
    test_policy_schema_validator_interface_keeps_validation_report_local_to_policy();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}