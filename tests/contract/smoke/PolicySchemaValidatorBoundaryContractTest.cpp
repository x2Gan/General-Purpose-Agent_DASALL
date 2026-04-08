#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "policy/PolicySchemaValidator.h"
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

dasall::infra::policy::PolicyRuleDescriptor make_rule() {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("policy-validator-boundary-rule"),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("ops"),
      .action = std::string("apply_patch"),
      .target_selector = std::string("policy.current"),
      .effect = dasall::infra::policy::PolicyEffect::Deny,
      .priority = 5,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("ticket_approved")},
      .reason_code = std::string("policy_admin_guard"),
  };
}

void test_policy_schema_validator_boundary_keeps_local_validation_report_shape() {
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::infra::policy::ValidationReport;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ValidationReport{}.blocking_errors), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ValidationReport{}.warnings), std::vector<std::string>>);
  static_assert(!HasResultCodeField<ValidationReport> && !HasErrorInfoField<ValidationReport>);

  const PolicySchemaValidator validator;
  const auto report = validator.validate_bundle(dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-validator-boundary-bundle"),
      .schema_version = std::string("2"),
      .source = std::string("source_id=defaults"),
      .checksum = std::string("sha256:boundary"),
      .rules = {make_rule()},
      .generated_at = std::string("2026-04-05T12:20:00Z"),
  });

  assert_true(report.has_blocking_errors() && report.field_paths.front() == "schema_version",
              "PolicySchemaValidator should keep implementation failures inside the local ValidationReport field-path boundary");
}

void test_policy_schema_validator_boundary_keeps_patch_failures_inside_policy_report() {
  using dasall::infra::policy::PolicyPatch;
  using dasall::infra::policy::PolicyPatchOperation;
  using dasall::infra::policy::PolicyPatchOperationType;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::tests::support::assert_true;

  const PolicySchemaValidator validator;
  const auto report = validator.validate_patch(
      dasall::infra::policy::PolicySnapshot{
          .snapshot_id = std::string("snapshot-validator-boundary-001"),
          .generation = 8,
          .version = std::string("policy-v8"),
          .mode = dasall::infra::policy::PolicyMode::Enforced,
          .effective_rules = {make_rule()},
          .created_at = std::string("2026-04-05T12:25:00Z"),
          .source_chain = {std::string("defaults")},
          .last_known_good_ref = std::string("snapshot-validator-boundary-000"),
      },
      PolicyPatch{
          .patch_id = std::string("policy-validator-boundary-patch"),
          .base_generation = 8,
          .operations = {PolicyPatchOperation{
              .operation = PolicyPatchOperationType::UpdateMode,
              .rule_id = std::string(),
              .rule = std::nullopt,
              .mode = dasall::infra::policy::PolicyMode::Unspecified,
          }},
          .actor = std::string("ops-user"),
          .reason = std::string("maintenance-window"),
      });

  assert_true(report.has_blocking_errors() && report.field_paths.front() == "operations[0].mode",
              "PolicySchemaValidator should keep patch shape failures inside the local ValidationReport strings instead of contracts error objects");
}

}  // namespace

int main() {
  try {
    test_policy_schema_validator_boundary_keeps_local_validation_report_shape();
    test_policy_schema_validator_boundary_keeps_patch_failures_inside_policy_report();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}