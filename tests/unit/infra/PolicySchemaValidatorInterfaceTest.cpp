#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "policy/IPolicySchemaValidator.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasLoadFromSourcesMethod = requires {
  &T::load_from_sources;
};

template <typename T>
concept HasCommitMethod = requires {
  &T::commit;
};

template <typename T>
concept HasLoadPolicyMethod = requires {
  &T::load_policy;
};

template <typename T>
concept HasEvaluateMethod = requires {
  &T::evaluate;
};

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id,
                                                      dasall::infra::policy::PolicyDomain domain,
                                                      std::string reason_code) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = domain,
      .subject = std::string("ops"),
      .action = std::string("apply_patch"),
      .target_selector = std::string("policy.current"),
      .effect = dasall::infra::policy::PolicyEffect::Deny,
      .priority = 5,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("ticket_approved")},
      .reason_code = std::move(reason_code),
  };
}

dasall::infra::policy::PolicyBundle make_bundle(std::string schema_version = "1") {
  return dasall::infra::policy::PolicyBundle{
      .bundle_id = std::string("policy-bundle-validator-001"),
      .schema_version = std::move(schema_version),
      .source = std::string("source_id=infra/config/defaults/runtime_policy.yaml;version=defaults@1"),
      .checksum = std::string("sha256:policy-validator-001"),
      .rules = {make_rule("policy-validator-rule-001",
                          dasall::infra::policy::PolicyDomain::PolicyAdmin,
                          "policy_admin_guard")},
      .generated_at = std::string("2026-04-01T15:20:00Z"),
  };
}

dasall::infra::policy::PolicySnapshot make_snapshot(std::uint64_t generation) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::string("snapshot-validator-001"),
      .generation = generation,
      .version = std::string("policy-v") + std::to_string(generation),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {make_rule("policy-validator-rule-001",
                                    dasall::infra::policy::PolicyDomain::PolicyAdmin,
                                    "policy_admin_guard")},
      .created_at = std::string("2026-04-01T15:30:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::string("snapshot-validator-000"),
  };
}

dasall::infra::policy::PolicyPatch make_patch(std::uint64_t base_generation) {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("policy-patch-validator-001"),
      .base_generation = base_generation,
      .operations = {dasall::infra::policy::PolicyPatchOperation{
          .operation = dasall::infra::policy::PolicyPatchOperationType::UpdateMode,
          .rule_id = std::string(),
          .rule = std::nullopt,
          .mode = dasall::infra::policy::PolicyMode::Compatibility,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("compatibility window"),
  };
}

class StaticPolicySchemaValidator final : public dasall::infra::policy::IPolicySchemaValidator {
 public:
  [[nodiscard]] dasall::infra::policy::ValidationReport validate_bundle(
      const dasall::infra::policy::PolicyBundle& bundle) const override {
    if (!bundle.is_valid()) {
      return dasall::infra::policy::ValidationReport{
          .blocking_errors = {std::string("policy_bundle_invalid")},
          .warnings = {},
          .invalid_rule_ids = {std::string("bundle")},
          .field_paths = {std::string("bundle")},
      };
    }

    if (bundle.schema_version != "1") {
      return dasall::infra::policy::ValidationReport{
          .blocking_errors = {std::string("policy_schema_unsupported")},
          .warnings = {},
          .invalid_rule_ids = {bundle.rules.front().rule_id},
          .field_paths = {std::string("schema_version")},
      };
    }

    return dasall::infra::policy::ValidationReport{
        .blocking_errors = {},
        .warnings = {std::string("compat_mode_available")},
        .invalid_rule_ids = {},
        .field_paths = {},
    };
  }

  [[nodiscard]] dasall::infra::policy::ValidationReport validate_patch(
      const dasall::infra::policy::PolicySnapshot& current_snapshot,
      const dasall::infra::policy::PolicyPatch& patch) const override {
    if (!current_snapshot.is_valid()) {
      return dasall::infra::policy::ValidationReport{
          .blocking_errors = {std::string("policy_snapshot_missing")},
          .warnings = {},
          .invalid_rule_ids = {},
          .field_paths = {std::string("current_snapshot")},
      };
    }

    if (!patch.is_valid()) {
      return dasall::infra::policy::ValidationReport{
          .blocking_errors = {std::string("policy_patch_invalid")},
          .warnings = {},
          .invalid_rule_ids = {std::string("patch")},
          .field_paths = {std::string("operations")},
      };
    }

    if (current_snapshot.generation != patch.base_generation) {
      return dasall::infra::policy::ValidationReport{
          .blocking_errors = {std::string("patch_base_mismatch")},
          .warnings = {},
          .invalid_rule_ids = {},
          .field_paths = {std::string("base_generation")},
      };
    }

    return dasall::infra::policy::ValidationReport{};
  }
};

void test_policy_schema_validator_interface_keeps_two_frozen_validation_entrypoints() {
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
  static_assert(std::is_abstract_v<IPolicySchemaValidator>);

  StaticPolicySchemaValidator validator;

  const auto bundle_report = validator.validate_bundle(make_bundle());
  assert_true(!bundle_report.has_blocking_errors() && bundle_report.warnings.size() == 1,
              "IPolicySchemaValidator should accept a structurally valid bundle through the frozen validate_bundle entrypoint");

  const auto patch_report = validator.validate_patch(make_snapshot(7), make_patch(7));
  assert_true(!patch_report.has_blocking_errors(),
              "IPolicySchemaValidator should accept a patch whose base_generation matches the current snapshot generation");
}

void test_policy_schema_validator_interface_reports_locatable_failures_without_absorbing_other_roles() {
  using dasall::infra::policy::IPolicySchemaValidator;
  using dasall::tests::support::assert_true;

  StaticPolicySchemaValidator validator;

  const auto invalid_bundle_report = validator.validate_bundle(make_bundle("2"));
  assert_true(invalid_bundle_report.has_blocking_errors() &&
                  invalid_bundle_report.field_paths.front() == "schema_version",
              "IPolicySchemaValidator should keep unsupported schema failures locatable through field_paths");

  const auto invalid_patch_report = validator.validate_patch(make_snapshot(8), make_patch(7));
  assert_true(invalid_patch_report.has_blocking_errors() &&
                  invalid_patch_report.field_paths.front() == "base_generation",
              "IPolicySchemaValidator should keep patch base mismatches inside the local ValidationReport boundary");

  static_assert(!HasLoadFromSourcesMethod<IPolicySchemaValidator>);
  static_assert(!HasCommitMethod<IPolicySchemaValidator>);
  static_assert(!HasLoadPolicyMethod<IPolicySchemaValidator>);
  static_assert(!HasEvaluateMethod<IPolicySchemaValidator>);

  assert_true(std::has_virtual_destructor_v<IPolicySchemaValidator>,
              "IPolicySchemaValidator should keep a virtual destructor as the only lifecycle requirement of the pure abstract boundary");
  assert_true(!std::is_default_constructible_v<IPolicySchemaValidator>,
              "IPolicySchemaValidator should stay focused on validation and should not absorb loader, store, or manager responsibilities");
}

}  // namespace

int main() {
  try {
    test_policy_schema_validator_interface_keeps_two_frozen_validation_entrypoints();
    test_policy_schema_validator_interface_reports_locatable_failures_without_absorbing_other_roles();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}