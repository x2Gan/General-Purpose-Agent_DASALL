#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "policy/PolicySchemaValidator.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
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

dasall::infra::policy::PolicyBundle make_bundle() {
  return dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-bundle-validator-001"),
      .schema_version = std::string("1"),
      .source = std::string("source_id=infra/config/defaults/runtime_policy.yaml;version=defaults@1"),
      .checksum = std::string("sha256:policy-validator-001"),
      .rules = {make_rule("policy-validator-rule-001")},
      .generated_at = std::string("2026-04-05T12:10:00Z"),
  };
}

dasall::infra::policy::PolicySnapshot make_snapshot(std::uint64_t generation) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("snapshot-validator-") + std::to_string(generation),
      .generation = generation,
      .version = std::string("policy-v") + std::to_string(generation),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {make_rule(std::string("policy-validator-rule-") +
                                    std::to_string(generation))},
      .created_at = std::string("2026-04-05T12:15:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("snapshot-validator-") +
                             std::to_string(generation - 1),
  };
}

dasall::infra::policy::PolicyPatch make_patch(std::uint64_t base_generation) {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("policy-patch-validator-001"),
      .base_generation = base_generation,
      .operations = {dasall::infra::policy::PolicyPatchOperation{
          .operation = dasall::infra::policy::PolicyPatchOperationType::ReplaceRule,
          .rule_id = std::string("policy-validator-rule-001"),
          .rule = make_rule("policy-validator-rule-001"),
          .mode = dasall::infra::policy::PolicyMode::Unspecified,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("maintenance-window"),
  };
}

void test_policy_schema_validator_accepts_valid_bundle_and_patch() {
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::tests::support::assert_true;

  const PolicySchemaValidator validator;

  const auto bundle_report = validator.validate_bundle(make_bundle());
  assert_true(!bundle_report.has_blocking_errors(),
              "PolicySchemaValidator should accept a structurally valid bundle");

  const auto patch_report = validator.validate_patch(make_snapshot(7), make_patch(7));
  assert_true(!patch_report.has_blocking_errors(),
              "PolicySchemaValidator should accept a structurally valid patch whose base_generation matches the current snapshot");
}

void test_policy_schema_validator_reports_missing_fields_unknown_domain_and_illegal_effect() {
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::tests::support::assert_true;

  const PolicySchemaValidator validator;

  auto missing_subject_bundle = make_bundle();
  missing_subject_bundle.rules.front().rule_id = "rule-missing-subject";
  missing_subject_bundle.rules.front().subject.clear();
  const auto missing_subject_report = validator.validate_bundle(missing_subject_bundle);
  assert_true(missing_subject_report.has_blocking_errors() &&
                  missing_subject_report.invalid_rule_ids.front() == "rule-missing-subject" &&
                  missing_subject_report.field_paths.front() == "rules[0].subject",
              "PolicySchemaValidator should report missing required rule fields through invalid_rule_ids and field_paths");

  auto unknown_domain_bundle = make_bundle();
  unknown_domain_bundle.rules.front().rule_id = "rule-unknown-domain";
  unknown_domain_bundle.rules.front().domain = PolicyDomain::Unspecified;
  const auto unknown_domain_report = validator.validate_bundle(unknown_domain_bundle);
  assert_true(unknown_domain_report.has_blocking_errors() &&
                  unknown_domain_report.field_paths.front() == "rules[0].domain",
              "PolicySchemaValidator should reject rules whose domain falls outside the frozen enum set");

  auto illegal_effect_bundle = make_bundle();
  illegal_effect_bundle.rules.front().rule_id = "rule-illegal-effect";
  illegal_effect_bundle.rules.front().effect = PolicyEffect::Unspecified;
  const auto illegal_effect_report = validator.validate_bundle(illegal_effect_bundle);
  assert_true(illegal_effect_report.has_blocking_errors() &&
                  illegal_effect_report.field_paths.front() == "rules[0].effect",
              "PolicySchemaValidator should reject rules whose effect falls outside the frozen enum set");
}

void test_policy_schema_validator_reports_schema_and_patch_base_mismatch_failures() {
  using dasall::infra::policy::PolicySchemaValidator;
  using dasall::tests::support::assert_true;

  const PolicySchemaValidator validator;

  auto unsupported_schema_bundle = make_bundle();
  unsupported_schema_bundle.schema_version = "2";
  const auto unsupported_schema_report = validator.validate_bundle(unsupported_schema_bundle);
  assert_true(unsupported_schema_report.has_blocking_errors() &&
                  unsupported_schema_report.field_paths.front() == "schema_version",
              "PolicySchemaValidator should keep schema incompatibilities locatable through schema_version field paths");

  const auto mismatched_patch_report = validator.validate_patch(make_snapshot(9), make_patch(7));
  assert_true(mismatched_patch_report.has_blocking_errors() &&
                  mismatched_patch_report.field_paths.front() == "base_generation",
              "PolicySchemaValidator should reject patches whose base_generation does not match the current snapshot");
}

}  // namespace

int main() {
  try {
    test_policy_schema_validator_accepts_valid_bundle_and_patch();
    test_policy_schema_validator_reports_missing_fields_unknown_domain_and_illegal_effect();
    test_policy_schema_validator_reports_schema_and_patch_base_mismatch_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}