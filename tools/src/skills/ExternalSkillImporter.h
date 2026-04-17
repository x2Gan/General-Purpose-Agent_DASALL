#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "skills/SkillRegistry.h"

namespace dasall::tools::skills {

enum class SkillImportDiagnosticLevel {
  Info,
  Warning,
  Error,
};

struct SkillImportDiagnostic {
  SkillImportDiagnosticLevel level = SkillImportDiagnosticLevel::Error;
  std::string source_ref;
  std::string reason_code;
  std::string message;
};

struct ParsedSkillFrontmatter {
  std::filesystem::path skill_markdown_path;
  std::unordered_map<std::string, std::string> scalar_fields;
  std::unordered_map<std::string, std::vector<std::string>> list_fields;
  std::string body;
};

struct SkillImporterOptions {
  bool external_skill_import_enabled = false;
  std::filesystem::path project_root;
};

struct SkillImportResult {
  std::vector<SkillSpecAsset> imported_assets;
  std::vector<SkillImportDiagnostic> diagnostics;
  std::size_t scanned_entries = 0U;
};

class ExternalSkillImporter {
 public:
  explicit ExternalSkillImporter(SkillImporterOptions options);

  [[nodiscard]] SkillImportResult import_directory(
      std::string_view source_key,
      const std::filesystem::path& directory_path,
      std::optional<std::string> dialect_ref = std::nullopt) const;
  [[nodiscard]] static std::optional<ParsedSkillFrontmatter> parse_frontmatter(
      const std::filesystem::path& skill_markdown_path,
      std::vector<SkillImportDiagnostic>& diagnostics);
  [[nodiscard]] std::vector<SkillSpecAsset> normalize_assets(
      std::string_view source_key,
      const std::filesystem::path& directory_path,
      const std::optional<std::string>& dialect_ref,
      std::vector<SkillImportDiagnostic>& diagnostics) const;
  [[nodiscard]] static std::vector<SkillImportDiagnostic> emit_diagnostics(
      std::vector<SkillImportDiagnostic> diagnostics);

 private:
  [[nodiscard]] std::filesystem::path resolve_path(
      const std::filesystem::path& path) const;

  SkillImporterOptions options_;
};

}  // namespace dasall::tools::skills