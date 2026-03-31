#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "config/IConfigValidator.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::config::ConfigSnapshot make_valid_snapshot(std::uint64_t version) {
  return dasall::infra::config::ConfigSnapshot{
      .version = version,
      .checksum = std::string("sha256:cfg-validator-001"),
      .created_at = std::string("2026-03-31T09:00:00Z"),
      .data = {dasall::infra::config::TypedConfig{
          .key_path = std::string("infra.config.validation.strict"),
          .value_type = dasall::infra::config::ConfigValueType::Boolean,
          .serialized_value = std::string("true"),
          .schema_version = std::string("1"),
          .source_kind = dasall::infra::config::ConfigSourceKind::Profile,
          .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
          .secret_backed = false,
      }},
      .source_chain = {
          dasall::infra::config::ConfigLayerRef{
              .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
              .document_format = dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
              .version_ref = std::string("defaults@1"),
              .schema_version = std::string("1"),
          },
          dasall::infra::config::ConfigLayerRef{
              .source_kind = dasall::infra::config::ConfigSourceKind::Profile,
              .document_format = dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
              .version_ref = std::string("desktop_full@1"),
              .schema_version = std::string("1"),
          },
      },
  };
}

dasall::infra::config::ConfigPatch make_valid_patch(std::uint64_t base_version) {
  return dasall::infra::config::ConfigPatch{
      .patch_id = std::string("runtime-patch-validator-001"),
      .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/301"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = base_version,
      .reason_code = std::string("diagnostic_window"),
      .expires_at = std::string("2026-03-31T10:00:00Z"),
      .patches = {dasall::infra::config::ConfigPatchEntry{
          .op = dasall::infra::config::ConfigPatchOperation::Replace,
          .key_path = std::string("infra.config.validation.strict"),
          .value = dasall::infra::config::TypedConfig{
              .key_path = std::string("infra.config.validation.strict"),
              .value_type = dasall::infra::config::ConfigValueType::Boolean,
              .serialized_value = std::string("false"),
              .schema_version = std::string("1"),
              .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/301"),
              .secret_backed = false,
          },
      }},
  };
}

class NullConfigValidator final : public dasall::infra::config::IConfigValidator {
 public:
  [[nodiscard]] dasall::infra::config::ConfigValidationResult validate(
      const dasall::infra::config::ConfigSnapshot& snapshot) const override {
    if (!snapshot.is_valid()) {
      return dasall::infra::config::ConfigValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config snapshot must include versioned source_chain and typed entries",
          "config.validate",
          "NullConfigValidator",
          dasall::infra::config::ConfigValidationReport{
              .issues = {dasall::infra::config::ValidationIssue{
                  .key_path = std::string("snapshot"),
                  .code = std::string("cfg_invalid_snapshot"),
                  .severity = dasall::infra::config::ValidationSeverity::Error,
                  .message = std::string("config snapshot must stay structurally valid before validation"),
              }},
          });
    }

    return dasall::infra::config::ConfigValidationResult::success();
  }

  [[nodiscard]] dasall::infra::config::ConfigValidationResult validate_patch(
      const dasall::infra::config::ConfigSnapshot& current_snapshot,
      const dasall::infra::config::ConfigPatch& patch) const override {
    if (!current_snapshot.is_valid()) {
      return dasall::infra::config::ConfigValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "current snapshot must be valid before runtime patch validation",
          "config.validate_patch",
          "NullConfigValidator",
          dasall::infra::config::ConfigValidationReport{
              .issues = {dasall::infra::config::ValidationIssue{
                  .key_path = std::string("current_snapshot"),
                  .code = std::string("cfg_missing_current_snapshot"),
                  .severity = dasall::infra::config::ValidationSeverity::Error,
                  .message = std::string("current snapshot is required for runtime patch validation"),
              }},
          });
    }

    if (!patch.is_valid()) {
      return dasall::infra::config::ConfigValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "runtime patch metadata and mutable key paths are required",
          "config.validate_patch",
          "NullConfigValidator",
          dasall::infra::config::ConfigValidationReport{
              .issues = {dasall::infra::config::ValidationIssue{
                  .key_path = std::string("patches"),
                  .code = std::string("cfg_invalid_patch"),
                  .severity = dasall::infra::config::ValidationSeverity::Error,
                  .message = std::string("patch metadata or key paths violate the frozen override contract"),
              }},
          });
    }

    if (current_snapshot.version != patch.base_version) {
      return dasall::infra::config::ConfigValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "runtime patch base_version must match the active snapshot version",
          "config.validate_patch",
          "NullConfigValidator",
          dasall::infra::config::ConfigValidationReport{
              .issues = {dasall::infra::config::ValidationIssue{
                  .key_path = std::string("base_version"),
                  .code = std::string("cfg_base_version_mismatch"),
                  .severity = dasall::infra::config::ValidationSeverity::Error,
                  .message = std::string("patch base_version must match the current snapshot version"),
              }},
          });
    }

    return dasall::infra::config::ConfigValidationResult::success(
        dasall::infra::config::ConfigValidationReport{
            .issues = {dasall::infra::config::ValidationIssue{
                .key_path = std::string("infra.config.validation.strict"),
                .code = std::string("cfg_runtime_patch_review"),
                .severity = dasall::infra::config::ValidationSeverity::Warning,
                .message = std::string("runtime patch remains subject to apply-time policy gates"),
            }},
        });
  }
};

void test_config_validator_interface_accepts_valid_snapshot_and_patch_inputs() {
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigSnapshot;
  using dasall::infra::config::ConfigValidationReport;
  using dasall::infra::config::ConfigValidationResult;
  using dasall::infra::config::IConfigValidator;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<const IConfigValidator&>().validate(
                                   std::declval<const ConfigSnapshot&>())),
                               ConfigValidationResult>);
  static_assert(std::is_same_v<decltype(std::declval<const IConfigValidator&>().validate_patch(
                                   std::declval<const ConfigSnapshot&>(),
                                   std::declval<const ConfigPatch&>())),
                               ConfigValidationResult>);
  static_assert(std::is_same_v<decltype(ConfigValidationReport{}.issues),
                               std::vector<dasall::infra::config::ValidationIssue>>);

  NullConfigValidator validator;
  const auto snapshot = make_valid_snapshot(7);
  const auto patch = make_valid_patch(7);

  const auto snapshot_validation = validator.validate(snapshot);
  assert_true(snapshot_validation.accepted && snapshot_validation.report.is_valid() &&
                  !snapshot_validation.report.has_errors(),
              "IConfigValidator should accept a structurally valid versioned snapshot");

  const auto patch_validation = validator.validate_patch(snapshot, patch);
  assert_true(patch_validation.accepted && patch_validation.report.is_valid(),
              "IConfigValidator should accept a runtime patch whose base_version matches the active snapshot");
  assert_true(!patch_validation.report.has_errors(),
              "validator success path may emit warnings but must not emit blocking errors");
}

void test_config_validator_interface_reports_locatable_validation_failures() {
  using dasall::tests::support::assert_true;

  NullConfigValidator validator;

  const auto invalid_snapshot = validator.validate(dasall::infra::config::ConfigSnapshot{});
  assert_true(!invalid_snapshot.accepted,
              "IConfigValidator should reject an invalid snapshot before downstream merge or publish steps");
  assert_true(invalid_snapshot.references_only_contract_error_types(),
              "validator snapshot failures should remain inside contracts ResultCode/ErrorInfo types");
  assert_true(invalid_snapshot.report.has_errors() && invalid_snapshot.report.is_valid(),
              "validator snapshot failures should remain locatable through well-formed validation issues");

  const auto mismatched_patch = validator.validate_patch(make_valid_snapshot(9), make_valid_patch(8));
  assert_true(!mismatched_patch.accepted,
              "IConfigValidator should reject runtime patches whose base_version lags the active snapshot");
  assert_true(mismatched_patch.references_only_contract_error_types(),
              "validator patch failures should remain inside contracts ResultCode/ErrorInfo types");
  assert_true(mismatched_patch.report.has_errors() && mismatched_patch.report.issues.front().key_path == "base_version",
              "validator patch failures should identify the precise key path that triggered rejection");
}

}  // namespace

int main() {
  try {
    test_config_validator_interface_accepts_valid_snapshot_and_patch_inputs();
    test_config_validator_interface_reports_locatable_validation_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}