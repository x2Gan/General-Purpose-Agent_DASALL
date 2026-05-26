#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "IKnowledgeService.h"
#include "health/FreshnessController.h"
#include "health/KnowledgeTelemetry.h"
#include "index/IndexWriter.h"
#include "retrieve/IQueryEncoder.h"
#include "retrieve/IVectorRecallStore.h"

namespace dasall::knowledge {

struct DenseStoreFactoryContext {
  std::filesystem::path snapshots_root;
  std::function<std::optional<IndexManifest>()> active_manifest;

  [[nodiscard]] bool has_consistent_values() const {
    return snapshots_root.is_absolute() && static_cast<bool>(active_manifest);
  }
};

struct InstalledAssetKnowledgeServiceOptions {
  std::filesystem::path readonly_assets_root;
  std::filesystem::path state_root;
  std::string service_instance_id;
  std::string profile_id;
  TelemetrySinks telemetry_sinks;
  std::function<index::DenseSnapshotBuildResult(
      const index::DenseSnapshotBuildRequest& request)>
      build_dense_snapshot;
  std::function<std::unique_ptr<retrieve::IVectorRecallStore>(
      const DenseStoreFactoryContext& context)>
      create_vector_recall_store;
  std::function<std::unique_ptr<retrieve::IQueryEncoder>()> create_query_encoder;
  std::vector<std::string> runtime_canary_allowed_corpora;

  [[nodiscard]] bool has_consistent_values() const {
    return readonly_assets_root.is_absolute() && state_root.is_absolute() &&
           !service_instance_id.empty();
  }
};

struct KnowledgeServiceFactoryResult {
  std::shared_ptr<IKnowledgeService> service;
  std::string error;

  [[nodiscard]] bool ok() const {
    return service != nullptr && error.empty();
  }
};

[[nodiscard]] KnowledgeServiceFactoryResult create_installed_asset_knowledge_service(
    const InstalledAssetKnowledgeServiceOptions& options);

}  // namespace dasall::knowledge
