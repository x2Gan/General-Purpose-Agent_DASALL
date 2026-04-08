#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "config/ConfigErrors.h"
#include "config/ConfigValidator.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::config::TypedConfig make_entry(std::string key_path,
                                                            dasall::infra::config::ConfigValueType value_type,
                                                            std::string serialized_value) {
  return dasall::infra::config::TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string("1"),
      .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
      .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
      .secret_backed = false,
  };
}

[[nodiscard]] dasall::infra::config::ConfigSnapshot make_snapshot(
    std::vector<dasall::infra::config::TypedConfig> data,
    std::uint64_t version = 7) {
  return dasall::infra::config::ConfigSnapshot{
      .version = version,
      .checksum = std::string("sha256:cfg-validator"),
      .created_at = std::string("2026-04-02T00:00:00Z"),
      .data = std::move(data),
      .source_chain = {dasall::infra::config::ConfigLayerRef{
          .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
          .document_format = dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
          .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
          .version_ref = std::string("defaults@1"),
          .schema_version = std::string("1"),
      }},
  };
}

[[nodiscard]] dasall::infra::config::ConfigPatch make_patch(std::uint64_t base_version,
                                                            std::string key_path,
                                                            dasall::infra::config::ConfigValueType value_type,
                                                            std::string serialized_value) {
  return dasall::infra::config::ConfigPatch{
      .patch_id = std::string("runtime-patch-validator-010"),
      .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/310"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = base_version,
      .reason_code = std::string("diagnostic_window"),
      .expires_at = std::string("2026-04-02T02:00:00Z"),
      .patches = {dasall::infra::config::ConfigPatchEntry{
          .op = dasall::infra::config::ConfigPatchOperation::Replace,
          .key_path = key_path,
          .value = dasall::infra::config::TypedConfig{
              .key_path = key_path,
              .value_type = value_type,
              .serialized_value = std::move(serialized_value),
              .schema_version = std::string("1"),
              .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/310"),
              .secret_backed = false,
          },
      }},
  };
}

[[nodiscard]] bool has_issue(const dasall::infra::config::ConfigValidationReport& report,
                             std::string_view key_path,
                             std::string_view code_prefix) {
  return std::any_of(report.issues.begin(), report.issues.end(), [&](const auto& issue) {
    return issue.key_path == key_path && issue.code.starts_with(code_prefix);
  });
}

void test_config_validator_accepts_valid_snapshot_and_allowlisted_patch() {
  using dasall::infra::config::ConfigValidator;
  using dasall::infra::config::ConfigValueType;
  using dasall::tests::support::assert_true;

  ConfigValidator validator;
  const auto snapshot = make_snapshot({
      make_entry("infra.config.validation.strict", ConfigValueType::Boolean, "true"),
      make_entry("infra.config.watch.enabled", ConfigValueType::Boolean, "true"),
      make_entry("infra.config.watch.debounce_ms", ConfigValueType::UnsignedInteger, "500"),
      make_entry("infra.config.cache.ttl_ms", ConfigValueType::UnsignedInteger, "30000"),
      make_entry("infra.config.runtime_patch.enabled", ConfigValueType::Boolean, "true"),
      make_entry("infra.config.runtime_patch.allowlist",
                 ConfigValueType::StringList,
                 "[infra.config.watch.,ops_policy.log_level]"),
      make_entry("infra.config.rollback.enabled", ConfigValueType::Boolean, "true"),
      make_entry("infra.config.source.external.enabled", ConfigValueType::Boolean, "false"),
      make_entry("infra.config.source.external.timeout_ms",
                 ConfigValueType::UnsignedInteger,
                 "1000"),
  });

  const auto snapshot_result = validator.validate(snapshot);
  assert_true(snapshot_result.accepted && snapshot_result.report.is_valid() &&
                  !snapshot_result.report.has_errors(),
              "ConfigValidator should accept a snapshot that satisfies the frozen type and range rules");

  const auto patch_result = validator.validate_patch(
      snapshot,
      make_patch(snapshot.version,
                 std::string("infra.config.watch.debounce_ms"),
                 ConfigValueType::UnsignedInteger,
                 std::string("750")));
  assert_true(patch_result.accepted && patch_result.report.is_valid() &&
                  !patch_result.report.has_errors(),
              "ConfigValidator should accept an allowlisted runtime patch whose type and range remain valid");
}

void test_config_validator_reports_type_range_and_mutual_exclusion_failures() {
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigValidator;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigValidator validator;
  const auto invalid_snapshot = make_snapshot({
      make_entry("infra.config.watch.debounce_ms", ConfigValueType::String, "fast"),
      make_entry("infra.config.runtime_patch.enabled", ConfigValueType::Boolean, "false"),
      make_entry("infra.config.runtime_patch.allowlist",
                 ConfigValueType::StringList,
                 "[infra.config.watch.]"),
      make_entry("infra.config.source.external.timeout_ms",
                 ConfigValueType::UnsignedInteger,
                 "0"),
  });

  const auto result = validator.validate(invalid_snapshot);
  assert_true(!result.accepted && result.references_only_contract_error_types(),
              "ConfigValidator should reject snapshots that violate frozen type, range, or mutual exclusion rules");
  assert_true(result.result_code == map_config_error_code(ConfigErrorCode::TypeMismatch).result_code,
              "ConfigValidator should map type-led snapshot failures to the frozen config type mismatch category");
  assert_true(has_issue(result.report, "infra.config.watch.debounce_ms", "cfg_type_") &&
                  has_issue(result.report, "infra.config.source.external.timeout_ms", "cfg_range_") &&
                  has_issue(result.report, "infra.config.runtime_patch.allowlist", "cfg_conflict_"),
              "ConfigValidator should emit locatable issues for type, range, and mutual exclusion failures");
}

void test_config_validator_rejects_non_allowlisted_and_type_mismatched_patches() {
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigValidator;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigValidator validator;
  const auto snapshot = make_snapshot({
      make_entry("infra.config.watch.debounce_ms", ConfigValueType::UnsignedInteger, "500"),
      make_entry("infra.config.runtime_patch.enabled", ConfigValueType::Boolean, "true"),
      make_entry("infra.config.runtime_patch.allowlist",
                 ConfigValueType::StringList,
                 "[ops_policy.log_level]"),
  });

  const auto denied_patch = validator.validate_patch(
      snapshot,
      make_patch(snapshot.version,
                 std::string("infra.config.watch.debounce_ms"),
                 ConfigValueType::UnsignedInteger,
                 std::string("750")));
  assert_true(!denied_patch.accepted && denied_patch.references_only_contract_error_types(),
              "ConfigValidator should reject runtime patches outside the frozen allowlist prefixes");
  assert_true(denied_patch.result_code ==
                  map_config_error_code(ConfigErrorCode::ApplyRejected).result_code &&
                  has_issue(denied_patch.report, "infra.config.watch.debounce_ms", "cfg_patch_denied_"),
              "ConfigValidator should map non-allowlisted runtime patches to the frozen apply rejected category");

  const auto type_mismatch_patch = validator.validate_patch(
      snapshot,
      make_patch(snapshot.version,
                 std::string("ops_policy.log_level"),
                 ConfigValueType::UnsignedInteger,
                 std::string("1")));
  assert_true(!type_mismatch_patch.accepted,
              "ConfigValidator should reject runtime patches whose declared type mismatches the current snapshot entry");
  assert_true(type_mismatch_patch.result_code ==
                  map_config_error_code(ConfigErrorCode::NotFound).result_code,
              "ConfigValidator should report missing runtime patch keys before type comparison when the key is absent");
}

}  // namespace

int main() {
  try {
    test_config_validator_accepts_valid_snapshot_and_allowlisted_patch();
    test_config_validator_reports_type_range_and_mutual_exclusion_failures();
    test_config_validator_rejects_non_allowlisted_and_type_mismatched_patches();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}