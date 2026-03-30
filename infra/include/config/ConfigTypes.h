#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::config {

inline constexpr std::string_view kConfigSchemaVersionV1 = "1";

inline constexpr std::array<std::string_view, 5> kSupportedProfileIds{
    "desktop_full",
    "cloud_full",
    "edge_balanced",
    "edge_minimal",
    "factory_test",
};

inline constexpr std::array<std::string_view, 11> kFrozenProfileTopLevelKeys{
    "profile_meta",
    "enabled_modules",
    "runtime_budget",
    "model_profile",
    "token_budget_policy",
    "prompt_policy",
    "capability_cache_policy",
    "degrade_policy",
    "timeout_policy",
    "execution_policy",
    "ops_policy",
};

inline constexpr std::array<std::string_view, 4> kFrozenProfileMetaRequiredKeys{
    "profile_id",
    "schema_version",
    "target_platform",
    "support_level",
};

[[nodiscard]] inline bool is_supported_config_schema_version(std::string_view schema_version) {
  return schema_version == kConfigSchemaVersionV1;
}

[[nodiscard]] inline bool is_supported_profile_id(std::string_view profile_id) {
  return std::find(kSupportedProfileIds.begin(), kSupportedProfileIds.end(), profile_id) !=
         kSupportedProfileIds.end();
}

[[nodiscard]] inline bool is_frozen_profile_top_level_key(std::string_view key) {
  return std::find(kFrozenProfileTopLevelKeys.begin(),
                   kFrozenProfileTopLevelKeys.end(),
                   key) != kFrozenProfileTopLevelKeys.end();
}

[[nodiscard]] inline bool is_runtime_override_protected_path(std::string_view key_path) {
  return key_path == "schema_version" || key_path.starts_with("profile_meta.") ||
         key_path.starts_with("enabled_modules.") ||
         key_path.starts_with("enabled_adapters.");
}

enum class ConfigValueType {
  Unspecified = 0,
  Boolean = 1,
  Integer = 2,
  UnsignedInteger = 3,
  String = 4,
  DurationMs = 5,
  StringList = 6,
  StringMap = 7,
  Object = 8,
};

enum class ConfigDefaultPolicy {
  Unspecified = 0,
  FailIfMissing = 1,
  ReturnFallback = 2,
  ReturnLastKnownGood = 3,
};

enum class ConfigSourceKind {
  Unspecified = 0,
  Defaults = 1,
  Profile = 2,
  DeploymentOverride = 3,
  RuntimeOverride = 4,
};

enum class ConfigDocumentFormat {
  Unspecified = 0,
  RuntimePolicyYamlV1 = 1,
  DeploymentOverlayYamlV1 = 2,
  RuntimeOverridePatchV1 = 3,
};

enum class ConfigPatchOperation {
  Unspecified = 0,
  Replace = 1,
  Remove = 2,
};

enum class ValidationSeverity {
  Unspecified = 0,
  Warning = 1,
  Error = 2,
};

[[nodiscard]] inline bool is_managed_override_source(ConfigSourceKind source_kind) {
  return source_kind == ConfigSourceKind::DeploymentOverride ||
         source_kind == ConfigSourceKind::RuntimeOverride;
}

struct TypedConfig {
  std::string key_path;
  ConfigValueType value_type = ConfigValueType::Unspecified;
  std::string serialized_value;
  std::string schema_version = std::string(kConfigSchemaVersionV1);
  ConfigSourceKind source_kind = ConfigSourceKind::Unspecified;
  std::string source_id;
  bool secret_backed = false;

  [[nodiscard]] bool has_supported_schema_version() const {
    return is_supported_config_schema_version(schema_version);
  }

  [[nodiscard]] bool is_valid() const {
    return !key_path.empty() && value_type != ConfigValueType::Unspecified &&
           !serialized_value.empty() && has_supported_schema_version() &&
           source_kind != ConfigSourceKind::Unspecified && !source_id.empty();
  }
};

struct ConfigQuery {
  std::string key_path;
  ConfigValueType expected_type = ConfigValueType::Unspecified;
  ConfigDefaultPolicy default_policy = ConfigDefaultPolicy::Unspecified;
  std::string fallback_serialized_value;

  [[nodiscard]] bool is_valid() const {
    if (key_path.empty() || expected_type == ConfigValueType::Unspecified ||
        default_policy == ConfigDefaultPolicy::Unspecified) {
      return false;
    }

    if (default_policy == ConfigDefaultPolicy::ReturnFallback) {
      return !fallback_serialized_value.empty();
    }

    return true;
  }
};

struct ConfigLayerRef {
  ConfigSourceKind source_kind = ConfigSourceKind::Unspecified;
  ConfigDocumentFormat document_format = ConfigDocumentFormat::Unspecified;
  std::string source_id;
  std::string version_ref;
  std::string schema_version = std::string(kConfigSchemaVersionV1);

  [[nodiscard]] bool matches_frozen_format() const {
    switch (source_kind) {
      case ConfigSourceKind::Defaults:
      case ConfigSourceKind::Profile:
        return document_format == ConfigDocumentFormat::RuntimePolicyYamlV1;
      case ConfigSourceKind::DeploymentOverride:
        return document_format == ConfigDocumentFormat::DeploymentOverlayYamlV1;
      case ConfigSourceKind::RuntimeOverride:
        return document_format == ConfigDocumentFormat::RuntimeOverridePatchV1;
      case ConfigSourceKind::Unspecified:
        break;
    }

    return false;
  }

  [[nodiscard]] bool is_valid() const {
    return source_kind != ConfigSourceKind::Unspecified && matches_frozen_format() &&
           !source_id.empty() && !version_ref.empty() &&
           is_supported_config_schema_version(schema_version);
  }
};

struct ConfigPatchEntry {
  ConfigPatchOperation op = ConfigPatchOperation::Unspecified;
  std::string key_path;
  std::optional<TypedConfig> value;

  [[nodiscard]] bool is_valid() const {
    if (op == ConfigPatchOperation::Unspecified || key_path.empty()) {
      return false;
    }

    if (op == ConfigPatchOperation::Replace) {
      return value.has_value() && value->is_valid() && value->key_path == key_path;
    }

    return op == ConfigPatchOperation::Remove && !value.has_value();
  }
};

struct ConfigPatch {
  std::string patch_id;
  ConfigSourceKind source_kind = ConfigSourceKind::Unspecified;
  std::string source_id;
  std::string actor;
  std::string target_scope;
  std::uint64_t base_version = 0;
  std::string reason_code;
  std::string expires_at;
  std::vector<ConfigPatchEntry> patches;

  [[nodiscard]] bool targets_only_mutable_paths() const {
    return std::none_of(patches.begin(), patches.end(), [](const ConfigPatchEntry& patch) {
      return is_runtime_override_protected_path(patch.key_path);
    });
  }

  [[nodiscard]] bool is_valid() const {
    if (patch_id.empty() || !is_managed_override_source(source_kind) || source_id.empty() ||
        actor.empty() || target_scope.empty() || base_version == 0 || reason_code.empty() ||
        patches.empty()) {
      return false;
    }

    if (source_kind == ConfigSourceKind::RuntimeOverride && expires_at.empty()) {
      return false;
    }

    return std::all_of(patches.begin(), patches.end(), [](const ConfigPatchEntry& patch) {
             return patch.is_valid();
           }) &&
           targets_only_mutable_paths();
  }
};

struct ConfigSnapshot {
  std::uint64_t version = 0;
  std::string checksum;
  std::string created_at;
  std::vector<TypedConfig> data;
  std::vector<ConfigLayerRef> source_chain;

  [[nodiscard]] bool has_unique_source_chain() const {
    std::vector<ConfigSourceKind> seen;
    seen.reserve(source_chain.size());

    for (const auto& layer : source_chain) {
      if (std::find(seen.begin(), seen.end(), layer.source_kind) != seen.end()) {
        return false;
      }
      seen.push_back(layer.source_kind);
    }

    return true;
  }

  [[nodiscard]] bool is_valid() const {
    if (version == 0 || checksum.empty() || created_at.empty() || data.empty() ||
        source_chain.empty() || source_chain.size() > 4 || !has_unique_source_chain()) {
      return false;
    }

    return std::all_of(data.begin(), data.end(), [](const TypedConfig& typed_config) {
             return typed_config.is_valid();
           }) &&
           std::all_of(source_chain.begin(), source_chain.end(), [](const ConfigLayerRef& layer) {
             return layer.is_valid();
           });
  }
};

struct ConfigDiffEntry {
  std::string key_path;
  std::string from_serialized_value;
  std::string to_serialized_value;
  ConfigSourceKind source_kind = ConfigSourceKind::Unspecified;

  [[nodiscard]] bool is_valid() const {
    return !key_path.empty() && source_kind != ConfigSourceKind::Unspecified &&
           from_serialized_value != to_serialized_value;
  }
};

struct ConfigDiff {
  std::uint64_t from_version = 0;
  std::uint64_t to_version = 0;
  std::vector<ConfigDiffEntry> changes;

  [[nodiscard]] bool is_valid() const {
    return from_version > 0 && to_version > from_version && !changes.empty() &&
           std::all_of(changes.begin(), changes.end(), [](const ConfigDiffEntry& change) {
             return change.is_valid();
           });
  }
};

struct ValidationIssue {
  std::string key_path;
  std::string code;
  ValidationSeverity severity = ValidationSeverity::Unspecified;
  std::string message;

  [[nodiscard]] bool is_valid() const {
    return !key_path.empty() && !code.empty() && severity != ValidationSeverity::Unspecified &&
           !message.empty();
  }
};

struct ConfigApplyResult {
  bool applied = false;
  std::string rollback_token;
  std::vector<std::string> warnings;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ConfigApplyResult success(std::string rollback_token = {},
                                                 std::vector<std::string> warnings = {}) {
    return ConfigApplyResult{
        .applied = true,
        .rollback_token = std::move(rollback_token),
        .warnings = std::move(warnings),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ConfigApplyResult failure(contracts::ResultCode result_code,
                                                 std::string message,
                                                 std::string stage,
                                                 std::string source_ref,
                                                 std::vector<std::string> warnings = {}) {
    return ConfigApplyResult{
        .applied = false,
        .rollback_token = {},
        .warnings = std::move(warnings),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.config",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return applied;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

}  // namespace dasall::infra::config