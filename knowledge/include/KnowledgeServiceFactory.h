#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "IKnowledgeService.h"

namespace dasall::knowledge {

struct InstalledAssetKnowledgeServiceOptions {
  std::filesystem::path readonly_assets_root;
  std::filesystem::path state_root;
  std::string service_instance_id;

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
