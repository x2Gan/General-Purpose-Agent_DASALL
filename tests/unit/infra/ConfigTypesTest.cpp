#include <exception>
#include <iostream>
#include <string>

#include "config/ConfigTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_config_types_freeze_typed_config_patch_schema_and_profile_keys() {
  using dasall::infra::config::ConfigDefaultPolicy;
  using dasall::infra::config::ConfigDiff;
  using dasall::infra::config::ConfigDiffEntry;
  using dasall::infra::config::ConfigDocumentFormat;
  using dasall::infra::config::ConfigLayerRef;
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigPatchEntry;
  using dasall::infra::config::ConfigPatchOperation;
  using dasall::infra::config::ConfigQuery;
  using dasall::infra::config::ConfigSnapshot;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::TypedConfig;
  using dasall::infra::config::ValidationIssue;
  using dasall::infra::config::ValidationSeverity;
  using dasall::infra::config::is_frozen_profile_top_level_key;
  using dasall::infra::config::is_runtime_override_protected_path;
  using dasall::infra::config::is_supported_profile_id;
  using dasall::tests::support::assert_true;

  const TypedConfig typed_config{
      .key_path = std::string("infra.config.validation.strict"),
      .value_type = ConfigValueType::Boolean,
      .serialized_value = std::string("true"),
      .schema_version = std::string("1"),
      .source_kind = ConfigSourceKind::Profile,
      .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
      .secret_backed = false,
  };
  assert_true(typed_config.is_valid(),
              "typed config should remain valid once path, type, schema version, and source metadata are frozen");

  const ConfigQuery query{
      .key_path = typed_config.key_path,
      .expected_type = ConfigValueType::Boolean,
      .default_policy = ConfigDefaultPolicy::ReturnFallback,
      .fallback_serialized_value = std::string("false"),
  };
  assert_true(query.is_valid(),
              "config query should remain valid when expected type and fallback policy are both explicit");

  const ConfigPatch patch{
      .patch_id = std::string("runtime-patch-001"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/123"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 7,
      .reason_code = std::string("diagnostic_window"),
      .expires_at = std::string("2026-03-30T12:30:00Z"),
      .patches = {ConfigPatchEntry{
          .op = ConfigPatchOperation::Replace,
          .key_path = typed_config.key_path,
          .value = typed_config,
      }},
  };
  assert_true(patch.is_valid(),
              "runtime override patch should remain valid only with frozen metadata, ttl, and replace/remove entry contracts");

  const ConfigSnapshot snapshot{
      .version = 7,
      .checksum = std::string("sha256:cfg-001"),
      .created_at = std::string("2026-03-30T12:00:00Z"),
      .data = {typed_config},
      .source_chain = {
          ConfigLayerRef{
              .source_kind = ConfigSourceKind::Defaults,
              .document_format = ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
              .version_ref = std::string("defaults@1"),
          },
          ConfigLayerRef{
              .source_kind = ConfigSourceKind::Profile,
              .document_format = ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
              .version_ref = std::string("desktop_full@1"),
          },
          ConfigLayerRef{
              .source_kind = ConfigSourceKind::DeploymentOverride,
              .document_format = ConfigDocumentFormat::DeploymentOverlayYamlV1,
              .source_id = std::string("deploy://site-001/config.yaml"),
              .version_ref = std::string("site-001@42"),
          },
          ConfigLayerRef{
              .source_kind = ConfigSourceKind::RuntimeOverride,
              .document_format = ConfigDocumentFormat::RuntimeOverridePatchV1,
              .source_id = std::string("ops://ticket/123"),
              .version_ref = std::string("patch-001"),
          },
      },
  };
  assert_true(snapshot.is_valid(),
              "config snapshot should stay valid when all four frozen source layers remain unique and format-constrained");

  const ConfigDiff diff{
      .from_version = 6,
      .to_version = 7,
      .changes = {ConfigDiffEntry{
          .key_path = typed_config.key_path,
          .from_serialized_value = std::string("false"),
          .to_serialized_value = std::string("true"),
          .source_kind = ConfigSourceKind::RuntimeOverride,
      }},
  };
  assert_true(diff.is_valid(),
              "config diff should keep key-granular changes and ordered version transitions");

  const ValidationIssue issue{
      .key_path = std::string("infra.config.runtime_patch.allowlist"),
      .code = std::string("cfg_allowlist_missing"),
      .severity = ValidationSeverity::Error,
      .message = std::string("runtime override allowlist must stay explicit"),
  };
  assert_true(issue.is_valid(),
              "validation issue should remain locatable once key path, code, severity, and message are all frozen");

  assert_true(is_supported_profile_id("desktop_full"),
              "desktop_full should remain one of the five frozen profile identifiers");
  assert_true(!is_supported_profile_id("staging"),
              "unsupported deploy environment aliases must not drift into frozen profile identifiers");
  assert_true(is_frozen_profile_top_level_key("enabled_modules"),
              "enabled_modules should remain a frozen top-level profile key");
  assert_true(is_runtime_override_protected_path("profile_meta.profile_id"),
              "runtime override should keep profile identity paths protected");
}

void test_config_types_reject_invalid_schema_forbidden_paths_and_duplicate_layers() {
  using dasall::infra::config::ConfigDefaultPolicy;
  using dasall::infra::config::ConfigDocumentFormat;
  using dasall::infra::config::ConfigLayerRef;
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigPatchEntry;
  using dasall::infra::config::ConfigPatchOperation;
  using dasall::infra::config::ConfigQuery;
  using dasall::infra::config::ConfigSnapshot;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::TypedConfig;
  using dasall::tests::support::assert_true;

  const TypedConfig invalid_schema{
      .key_path = std::string("profile_meta.profile_id"),
      .value_type = ConfigValueType::String,
      .serialized_value = std::string("desktop_full"),
      .schema_version = std::string("2"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/999"),
      .secret_backed = false,
  };
  assert_true(!invalid_schema.is_valid(),
              "typed config should reject unsupported schema versions before merge or apply");

  const ConfigQuery invalid_query{
      .key_path = std::string("infra.config.validation.strict"),
      .expected_type = ConfigValueType::Boolean,
      .default_policy = ConfigDefaultPolicy::ReturnFallback,
      .fallback_serialized_value = std::string(),
  };
  assert_true(!invalid_query.is_valid(),
              "config query should reject fallback policy without an explicit fallback value");

  const ConfigPatch forbidden_patch{
      .patch_id = std::string("runtime-patch-002"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/124"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 8,
      .reason_code = std::string("force_profile_change"),
      .expires_at = std::string("2026-03-30T13:00:00Z"),
      .patches = {ConfigPatchEntry{
          .op = ConfigPatchOperation::Replace,
          .key_path = std::string("profile_meta.profile_id"),
          .value = TypedConfig{
              .key_path = std::string("profile_meta.profile_id"),
              .value_type = ConfigValueType::String,
              .serialized_value = std::string("edge_minimal"),
              .schema_version = std::string("1"),
              .source_kind = ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/124"),
              .secret_backed = false,
          },
      }},
  };
  assert_true(!forbidden_patch.is_valid(),
              "runtime override should reject protected profile_meta and enabled_modules paths even when patch metadata is otherwise complete");

  const ConfigPatch remove_with_value{
      .patch_id = std::string("runtime-patch-003"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/125"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 8,
      .reason_code = std::string("bad_remove"),
      .expires_at = std::string("2026-03-30T13:05:00Z"),
      .patches = {ConfigPatchEntry{
          .op = ConfigPatchOperation::Remove,
          .key_path = std::string("infra.config.watch.enabled"),
          .value = TypedConfig{
              .key_path = std::string("infra.config.watch.enabled"),
              .value_type = ConfigValueType::Boolean,
              .serialized_value = std::string("false"),
              .schema_version = std::string("1"),
              .source_kind = ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/125"),
              .secret_backed = false,
          },
      }},
  };
  assert_true(!remove_with_value.is_valid(),
              "remove patch entries should reject carrying replacement values in the frozen patch contract");

  const ConfigSnapshot duplicate_profile_layers{
      .version = 9,
      .checksum = std::string("sha256:cfg-duplicate"),
      .created_at = std::string("2026-03-30T13:10:00Z"),
      .data = {TypedConfig{
          .key_path = std::string("infra.config.watch.enabled"),
          .value_type = ConfigValueType::Boolean,
          .serialized_value = std::string("true"),
          .schema_version = std::string("1"),
          .source_kind = ConfigSourceKind::Profile,
          .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
          .secret_backed = false,
      }},
      .source_chain = {
          ConfigLayerRef{
              .source_kind = ConfigSourceKind::Profile,
              .document_format = ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
              .version_ref = std::string("desktop_full@1"),
          },
          ConfigLayerRef{
              .source_kind = ConfigSourceKind::Profile,
              .document_format = ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("profiles/edge_balanced/runtime_policy.yaml"),
              .version_ref = std::string("edge_balanced@1"),
          },
      },
  };
  assert_true(!duplicate_profile_layers.is_valid(),
              "config snapshot should reject duplicate source kinds so the four-layer contract remains deterministic");
}

}  // namespace

int main() {
  try {
    test_config_types_freeze_typed_config_patch_schema_and_profile_keys();
    test_config_types_reject_invalid_schema_forbidden_paths_and_duplicate_layers();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}