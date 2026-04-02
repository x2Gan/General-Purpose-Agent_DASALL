#include "config/ConfigCenterFacade.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "config/ConfigErrors.h"

namespace dasall::infra::config {

namespace {

constexpr std::string_view kFacadeSourceRef = "ConfigCenterFacade";
constexpr std::string_view kSnapshotTimestamp = "2026-04-02T00:00:00Z";
constexpr std::string_view kFallbackSourceId = "config://fallback";

[[nodiscard]] std::string make_layer_version_ref(std::string_view prefix, std::uint64_t version) {
  return std::string(prefix) + "@" + std::to_string(version);
}

[[nodiscard]] std::string make_snapshot_checksum(std::uint64_t version) {
  return "config-snapshot-" + std::to_string(version);
}

[[nodiscard]] std::string make_rollback_token(std::uint64_t version) {
  return "rollback://config/" + std::to_string(version);
}

[[nodiscard]] ConfigApplyResult make_failure(ConfigErrorCode code,
                                             std::string message,
                                             std::string stage) {
  const ConfigErrorMapping mapping = map_config_error_code(code);
  return ConfigApplyResult::failure(mapping.result_code,
                                    std::string(config_error_code_name(code)) + ": " +
                                        std::move(message),
                                    std::move(stage),
                                    std::string(kFacadeSourceRef));
}

[[nodiscard]] ConfigLayerRef make_layer_ref(ConfigSourceKind source_kind,
                                            std::string source_id,
                                            std::uint64_t version) {
  ConfigDocumentFormat document_format = ConfigDocumentFormat::Unspecified;

  switch (source_kind) {
    case ConfigSourceKind::Defaults:
    case ConfigSourceKind::Profile:
      document_format = ConfigDocumentFormat::RuntimePolicyYamlV1;
      break;
    case ConfigSourceKind::DeploymentOverride:
      document_format = ConfigDocumentFormat::DeploymentOverlayYamlV1;
      break;
    case ConfigSourceKind::RuntimeOverride:
      document_format = ConfigDocumentFormat::RuntimeOverridePatchV1;
      break;
    case ConfigSourceKind::Unspecified:
      break;
  }

  return ConfigLayerRef{
      .source_kind = source_kind,
      .document_format = document_format,
      .source_id = std::move(source_id),
      .version_ref = make_layer_version_ref("v", version),
      .schema_version = std::string(kConfigSchemaVersionV1),
  };
}

[[nodiscard]] TypedConfig make_typed_config(std::string key_path,
                                            ConfigValueType value_type,
                                            std::string serialized_value,
                                            ConfigSourceKind source_kind,
                                            std::string source_id) {
  return TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string(kConfigSchemaVersionV1),
      .source_kind = source_kind,
      .source_id = std::move(source_id),
      .secret_backed = false,
  };
}

[[nodiscard]] ConfigSnapshot build_bootstrap_snapshot(const ConfigStartupContext& startup_context,
                                                      std::uint64_t version) {
  const std::string defaults_source_id = "infra/config/defaults/runtime_policy.yaml";
  const std::string profile_source_id =
      "profiles/" + startup_context.requested_profile_id + "/runtime_policy.yaml";

  ConfigSnapshot snapshot{
      .version = version,
      .checksum = make_snapshot_checksum(version),
      .created_at = std::string(kSnapshotTimestamp),
      .data = {
          make_typed_config("infra.config.validation.strict",
                            ConfigValueType::Boolean,
                            "true",
                            ConfigSourceKind::Defaults,
                            defaults_source_id),
          make_typed_config("profile_meta.profile_id",
                            ConfigValueType::String,
                            startup_context.requested_profile_id,
                            ConfigSourceKind::Profile,
                            profile_source_id),
          make_typed_config("infra.config.deployment.source_ref",
                            ConfigValueType::String,
                            startup_context.deployment_source_ref,
                            ConfigSourceKind::DeploymentOverride,
                            startup_context.deployment_source_ref),
      },
      .source_chain = {
          make_layer_ref(ConfigSourceKind::Defaults, defaults_source_id, version),
          make_layer_ref(ConfigSourceKind::Profile, profile_source_id, version),
          make_layer_ref(
              ConfigSourceKind::DeploymentOverride, startup_context.deployment_source_ref, version),
      },
  };

  if (startup_context.load_runtime_overlay &&
      !startup_context.runtime_overlay_source_ref.empty()) {
    snapshot.data.push_back(make_typed_config("infra.config.runtime.overlay_enabled",
                                              ConfigValueType::Boolean,
                                              "true",
                                              ConfigSourceKind::RuntimeOverride,
                                              startup_context.runtime_overlay_source_ref));
    snapshot.source_chain.push_back(make_layer_ref(ConfigSourceKind::RuntimeOverride,
                                                   startup_context.runtime_overlay_source_ref,
                                                   version));
  }

  return snapshot;
}

[[nodiscard]] std::vector<TypedConfig>::const_iterator find_config_entry(
    const ConfigSnapshot& snapshot,
    std::string_view key_path,
    ConfigValueType expected_type) {
  return std::find_if(snapshot.data.begin(), snapshot.data.end(), [&](const TypedConfig& entry) {
    return entry.key_path == key_path && entry.value_type == expected_type;
  });
}

[[nodiscard]] bool contains_protected_paths(const ConfigPatch& config_patch) {
  return std::any_of(config_patch.patches.begin(),
                     config_patch.patches.end(),
                     [](const ConfigPatchEntry& patch) {
                       return is_runtime_override_protected_path(patch.key_path);
                     });
}

void upsert_source_chain_layer(std::vector<ConfigLayerRef>& source_chain,
                               ConfigSourceKind source_kind,
                               const std::string& source_id,
                               std::uint64_t version) {
  const auto existing = std::find_if(source_chain.begin(),
                                     source_chain.end(),
                                     [source_kind](const ConfigLayerRef& layer) {
                                       return layer.source_kind == source_kind;
                                     });
  const ConfigLayerRef layer = make_layer_ref(source_kind, source_id, version);

  if (existing == source_chain.end()) {
    source_chain.push_back(layer);
    return;
  }

  *existing = layer;
}

[[nodiscard]] ConfigDiff build_diff(const ConfigSnapshot& before,
                                    const ConfigSnapshot& after,
                                    ConfigSourceKind source_kind,
                                    const ConfigPatch& patch) {
  ConfigDiff diff{
      .from_version = before.version,
      .to_version = after.version,
      .changes = {},
  };

  diff.changes.reserve(patch.patches.size());
  for (const auto& patch_entry : patch.patches) {
    const auto before_it = std::find_if(before.data.begin(), before.data.end(), [&](const TypedConfig& entry) {
      return entry.key_path == patch_entry.key_path;
    });
    const auto after_it = std::find_if(after.data.begin(), after.data.end(), [&](const TypedConfig& entry) {
      return entry.key_path == patch_entry.key_path;
    });

    const std::string from_serialized_value =
        before_it == before.data.end() ? std::string() : before_it->serialized_value;
    const std::string to_serialized_value =
        after_it == after.data.end() ? std::string() : after_it->serialized_value;

    if (from_serialized_value == to_serialized_value) {
      continue;
    }

    diff.changes.push_back(ConfigDiffEntry{
        .key_path = patch_entry.key_path,
        .from_serialized_value = from_serialized_value,
        .to_serialized_value = to_serialized_value,
        .source_kind = source_kind,
    });
  }

  return diff;
}
}  // namespace

bool ConfigCenterFacade::is_ready() const {
  return lifecycle_state_ == LifecycleState::Ready && current_snapshot_.is_valid();
}

ConfigApplyResult ConfigCenterFacade::load_layers(const ConfigStartupContext& startup_context) {
  if (!startup_context.is_valid()) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "startup context must keep a frozen profile_id and explicit actor_ref",
                        "config.load_layers");
  }

  if (is_ready()) {
    return make_failure(ConfigErrorCode::Conflict,
                        "config center skeleton only allows a single bootstrap load per lifecycle",
                        "config.load_layers");
  }

  if (startup_context.deployment_source_ref.empty()) {
    return make_failure(ConfigErrorCode::SourceUnavailable,
                        "deployment source_ref is required for the bootstrap chain",
                        "config.load_layers");
  }

  if (startup_context.load_runtime_overlay && startup_context.runtime_overlay_source_ref.empty()) {
    return make_failure(ConfigErrorCode::SourceUnavailable,
                        "runtime overlay source_ref is required when runtime overlay loading is enabled",
                        "config.load_layers");
  }

  current_snapshot_ = build_bootstrap_snapshot(startup_context, next_version_);
  if (!current_snapshot_.is_valid()) {
    current_snapshot_ = {};
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "bootstrap chain produced an invalid snapshot skeleton",
                        "config.load_layers");
  }

  lifecycle_state_ = LifecycleState::Ready;
  rollback_records_.clear();
  ++next_version_;
  return ConfigApplyResult::success();
}

std::optional<TypedConfig> ConfigCenterFacade::get_typed(const ConfigQuery& query) const {
  if (!is_ready() || !query.is_valid()) {
    return std::nullopt;
  }

  const auto current_it = find_config_entry(current_snapshot_, query.key_path, query.expected_type);
  if (current_it != current_snapshot_.data.end()) {
    return *current_it;
  }

  if (query.default_policy == ConfigDefaultPolicy::ReturnLastKnownGood) {
    for (auto it = rollback_records_.rbegin(); it != rollback_records_.rend(); ++it) {
      const auto rollback_match = find_config_entry(it->snapshot, query.key_path, query.expected_type);
      if (rollback_match != it->snapshot.data.end()) {
        return *rollback_match;
      }
    }
  }

  if (query.default_policy == ConfigDefaultPolicy::ReturnFallback) {
    return make_typed_config(query.key_path,
                             query.expected_type,
                             query.fallback_serialized_value,
                             ConfigSourceKind::Defaults,
                             std::string(kFallbackSourceId));
  }

  return std::nullopt;
}

ConfigApplyResult ConfigCenterFacade::apply_override(const ConfigPatch& config_patch) {
  if (!is_ready()) {
    return make_failure(ConfigErrorCode::NotFound,
                        "apply_override requires an active snapshot before runtime patching",
                        "config.apply_override");
  }

  if (contains_protected_paths(config_patch)) {
    return make_failure(ConfigErrorCode::ApplyRejected,
                        "runtime overrides must not target frozen profile or schema paths",
                        "config.apply_override");
  }

  if (!config_patch.is_valid()) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "runtime override patch must stay inside the frozen typed patch contract",
                        "config.apply_override");
  }

  if (config_patch.base_version != current_snapshot_.version) {
    return make_failure(ConfigErrorCode::Conflict,
                        "runtime override base_version must match the current snapshot version",
                        "config.apply_override");
  }

  for (const auto& patch_entry : config_patch.patches) {
    if (patch_entry.op != ConfigPatchOperation::Replace || !patch_entry.value.has_value()) {
      continue;
    }

    const auto existing = std::find_if(current_snapshot_.data.begin(),
                                       current_snapshot_.data.end(),
                                       [&](const TypedConfig& entry) {
                                         return entry.key_path == patch_entry.key_path;
                                       });
    if (existing != current_snapshot_.data.end() &&
        existing->value_type != patch_entry.value->value_type) {
      return make_failure(ConfigErrorCode::TypeMismatch,
                          "runtime override type must match the active typed config entry",
                          "config.apply_override");
    }
  }

  const std::string rollback_token = make_rollback_token(next_version_);
  rollback_records_.push_back(RollbackRecord{
      .token = rollback_token,
      .actor_ref = config_patch.actor,
      .snapshot = current_snapshot_,
  });

  ConfigSnapshot updated_snapshot = current_snapshot_;
  updated_snapshot.version = next_version_;
  updated_snapshot.checksum = make_snapshot_checksum(updated_snapshot.version);
  updated_snapshot.created_at = std::string(kSnapshotTimestamp);

  for (const auto& patch_entry : config_patch.patches) {
    auto existing = std::find_if(updated_snapshot.data.begin(),
                                 updated_snapshot.data.end(),
                                 [&](const TypedConfig& entry) {
                                   return entry.key_path == patch_entry.key_path;
                                 });

    if (patch_entry.op == ConfigPatchOperation::Remove) {
      if (existing != updated_snapshot.data.end()) {
        updated_snapshot.data.erase(existing);
      }
      continue;
    }

    TypedConfig next_value = *patch_entry.value;
    next_value.source_kind = config_patch.source_kind;
    next_value.source_id = config_patch.source_id;

    if (existing == updated_snapshot.data.end()) {
      updated_snapshot.data.push_back(std::move(next_value));
    } else {
      *existing = std::move(next_value);
    }
  }

  upsert_source_chain_layer(updated_snapshot.source_chain,
                            config_patch.source_kind,
                            config_patch.source_id,
                            updated_snapshot.version);

  if (!updated_snapshot.is_valid()) {
    rollback_records_.pop_back();
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "runtime override would leave the active snapshot invalid",
                        "config.apply_override");
  }

  const ConfigDiff diff = build_diff(current_snapshot_,
                                     updated_snapshot,
                                     config_patch.source_kind,
                                     config_patch);
  current_snapshot_ = std::move(updated_snapshot);
  ++next_version_;

  if (diff.is_valid()) {
    (void)publisher_.publish_config_changed(diff);
  }

  return ConfigApplyResult::success(rollback_token);
}

ConfigApplyResult ConfigCenterFacade::rollback(const ConfigRollbackToken& rollback_token) {
  if (!is_ready()) {
    return make_failure(ConfigErrorCode::RollbackFailed,
                        "rollback requires an active snapshot",
                        "config.rollback");
  }

  if (!rollback_token.is_valid()) {
    return make_failure(ConfigErrorCode::RollbackFailed,
                        "rollback token must stay explicit and attributable",
                        "config.rollback");
  }

  const auto rollback_it = std::find_if(rollback_records_.begin(),
                                        rollback_records_.end(),
                                        [&](const RollbackRecord& record) {
                                          return record.token == rollback_token.token &&
                                                 record.actor_ref == rollback_token.actor_ref;
                                        });
  if (rollback_it == rollback_records_.end()) {
    return make_failure(ConfigErrorCode::RollbackFailed,
                        "rollback token was not issued for the active config center lifecycle",
                        "config.rollback");
  }

  current_snapshot_ = rollback_it->snapshot;
  rollback_records_.erase(rollback_it);
  return ConfigApplyResult::success(rollback_token.token);
}

std::optional<ConfigSubscriptionHandle> ConfigCenterFacade::subscribe(
    const ConfigSubscriptionRequest& subscription_request) {
  return publisher_.subscribe(subscription_request);
}

}  // namespace dasall::infra::config