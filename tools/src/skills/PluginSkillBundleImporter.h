#pragma once

#include <filesystem>
#include <vector>

#include "bridge/PluginExtensionBridge.h"
#include "skills/ExternalSkillImporter.h"

namespace dasall::tools::skills {

class PluginSkillBundleImporter {
 public:
  explicit PluginSkillBundleImporter(SkillImporterOptions options);

  [[nodiscard]] SkillImportResult import_bundle(
      const bridge::SkillAssetRef& skill_asset_ref) const;

 private:
  [[nodiscard]] std::vector<SkillSpecAsset> import_internal_bundle(
      const bridge::SkillAssetRef& skill_asset_ref,
      std::vector<SkillImportDiagnostic>& diagnostics) const;
  [[nodiscard]] std::filesystem::path resolve_path(
      const std::filesystem::path& path) const;

  SkillImporterOptions options_;
  ExternalSkillImporter external_importer_;
};

}  // namespace dasall::tools::skills