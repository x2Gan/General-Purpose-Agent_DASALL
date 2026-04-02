#include "config/ConfigMerger.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "config/ConfigErrors.h"

namespace dasall::infra::config {
namespace {

constexpr std::string_view kMergedSnapshotTimestamp = "2026-04-02T00:00:00Z";

[[nodiscard]] int precedence_for_source_kind(ConfigSourceKind source_kind) {
  switch (source_kind) {
    case ConfigSourceKind::Defaults:
      return 0;
    case ConfigSourceKind::Profile:
      return 1;
    case ConfigSourceKind::DeploymentOverride:
      return 2;
    case ConfigSourceKind::RuntimeOverride:
      return 3;
    case ConfigSourceKind::Unspecified:
      break;
  }

  return -1;
}

[[nodiscard]] ConfigMergeResult make_failure(ConfigErrorCode code,
                                             std::string message,
                                             std::string stage,
                                             std::string source_ref) {
  const ConfigErrorMapping mapping = map_config_error_code(code);
  return ConfigMergeResult::failure(mapping.result_code,
                                    std::string(config_error_code_name(code)) + ": " +
                                        std::move(message),
                                    std::move(stage),
                                    std::move(source_ref));
}

[[nodiscard]] std::string make_checksum(const std::vector<ConfigLayerRef>& source_chain) {
  std::string checksum = "config-merge";
  for (const auto& layer : source_chain) {
    checksum += ":";
    checksum += layer.version_ref;
  }

  return checksum;
}

}  // namespace

ConfigMergeResult ConfigMerger::merge(const std::vector<ConfigLayerDocument>& layers) const {
  if (layers.empty() || layers.size() > 4U) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "merge expects between one and four config layers",
                        "config.merge",
                        "ConfigMerger");
  }

  std::vector<TypedConfig> merged_data;
  std::vector<ConfigLayerRef> source_chain;
  source_chain.reserve(layers.size());

  int previous_precedence = -1;
  for (const auto& layer : layers) {
    if (!layer.is_valid()) {
      return make_failure(ConfigErrorCode::InvalidSchema,
                          "config layer document must satisfy the frozen typed contract before merge",
                          "config.merge",
                          layer.layer_ref.source_id);
    }

    const int current_precedence = precedence_for_source_kind(layer.layer_ref.source_kind);
    if (current_precedence < 0 || current_precedence <= previous_precedence) {
      return make_failure(ConfigErrorCode::Conflict,
                          "config layers must be provided in defaults -> profile -> deploy -> runtime order without duplicates",
                          "config.merge",
                          layer.layer_ref.source_id);
    }
    previous_precedence = current_precedence;
    source_chain.push_back(layer.layer_ref);

    for (const auto& entry : layer.entries) {
      auto existing = std::find_if(merged_data.begin(), merged_data.end(), [&](const TypedConfig& candidate) {
        return candidate.key_path == entry.key_path;
      });

      if (existing == merged_data.end()) {
        merged_data.push_back(entry);
        continue;
      }

      if (existing->value_type != entry.value_type) {
        return make_failure(ConfigErrorCode::Conflict,
                            "conflicting value_type detected for key_path=" + entry.key_path,
                            "config.merge",
                            entry.source_id);
      }

      *existing = entry;
    }
  }

  ConfigSnapshot snapshot{
      .version = 1,
      .checksum = make_checksum(source_chain),
      .created_at = std::string(kMergedSnapshotTimestamp),
      .data = std::move(merged_data),
      .source_chain = std::move(source_chain),
  };
  if (!snapshot.is_valid()) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "merged snapshot did not satisfy the frozen config snapshot contract",
                        "config.merge",
                        "ConfigMerger");
  }

  return ConfigMergeResult::success(std::move(snapshot));
}

}  // namespace dasall::infra::config