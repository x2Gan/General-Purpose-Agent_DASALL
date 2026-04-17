#include "skills/PluginSkillBundleImporter.h"

#include <algorithm>

#include "skills/SkillImportSupport.h"

namespace dasall::tools::skills {

namespace {

void push_diagnostic(
    std::vector<SkillImportDiagnostic>& diagnostics,
    SkillImportDiagnosticLevel level,
    std::string source_ref,
    std::string reason_code,
    std::string message) {
  diagnostics.push_back(SkillImportDiagnostic{
      .level = level,
      .source_ref = std::move(source_ref),
      .reason_code = std::move(reason_code),
      .message = std::move(message),
  });
}

[[nodiscard]] std::vector<std::filesystem::path> collect_internal_asset_files(
    const std::filesystem::path& asset_root) {
  std::vector<std::filesystem::path> asset_files;
  if (!std::filesystem::exists(asset_root) || !std::filesystem::is_directory(asset_root)) {
    return asset_files;
  }

  for (const auto& entry : std::filesystem::directory_iterator(asset_root)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    if (entry.path().filename().string().ends_with(".skill.yaml")) {
      asset_files.push_back(entry.path());
    }
  }

  std::sort(asset_files.begin(), asset_files.end());
  return asset_files;
}

[[nodiscard]] std::vector<std::string> pick_list(
    const ParsedKeyValueDocument& document,
    std::string_view key) {
  const auto found = document.list_values.find(std::string(key));
  if (found == document.list_values.end()) {
    return {};
  }

  return unique_non_empty_values(found->second);
}

[[nodiscard]] std::optional<std::string> pick_scalar(
    const ParsedKeyValueDocument& document,
    std::string_view key) {
  const auto found = document.scalar_values.find(std::string(key));
  if (found == document.scalar_values.end() || found->second.empty()) {
    return std::nullopt;
  }

  return found->second;
}

}  // namespace

PluginSkillBundleImporter::PluginSkillBundleImporter(SkillImporterOptions options)
    : options_(std::move(options)),
      external_importer_(options_) {
  if (options_.project_root.empty()) {
    options_.project_root = std::filesystem::current_path();
  }
}

SkillImportResult PluginSkillBundleImporter::import_bundle(
    const bridge::SkillAssetRef& skill_asset_ref) const {
  SkillImportResult result;

  if (skill_asset_ref.source_key.empty()) {
    push_diagnostic(result.diagnostics, SkillImportDiagnosticLevel::Error,
                    skill_asset_ref.asset_root_ref,
                    "skill.bundle.source_missing",
                    "plugin skill bundle import requires a non-empty source_key");
    result.diagnostics = ExternalSkillImporter::emit_diagnostics(
        std::move(result.diagnostics));
    return result;
  }

  if (skill_asset_ref.asset_root_ref.empty()) {
    push_diagnostic(result.diagnostics, SkillImportDiagnosticLevel::Error,
                    skill_asset_ref.source_key,
                    "skill.bundle.asset_root_missing",
                    "plugin skill bundle import requires a non-empty asset_root_ref");
    result.diagnostics = ExternalSkillImporter::emit_diagnostics(
        std::move(result.diagnostics));
    return result;
  }

  if (!dialect_is_internal(skill_asset_ref.dialect_ref)) {
    return external_importer_.import_directory(
        skill_asset_ref.source_key,
        skill_asset_ref.asset_root_ref,
        skill_asset_ref.dialect_ref);
  }

  result.imported_assets = import_internal_bundle(skill_asset_ref, result.diagnostics);
  result.scanned_entries = result.imported_assets.size();
  result.diagnostics = ExternalSkillImporter::emit_diagnostics(std::move(result.diagnostics));
  return result;
}

std::vector<SkillSpecAsset> PluginSkillBundleImporter::import_internal_bundle(
    const bridge::SkillAssetRef& skill_asset_ref,
    std::vector<SkillImportDiagnostic>& diagnostics) const {
  std::vector<SkillSpecAsset> assets;
  const auto resolved_root = resolve_path(skill_asset_ref.asset_root_ref);
  if (!std::filesystem::exists(resolved_root) || !std::filesystem::is_directory(resolved_root)) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                    resolved_root.generic_string(), "skill.bundle.asset_root_missing",
                    "plugin skill asset root does not exist");
    return assets;
  }

  const auto asset_files = collect_internal_asset_files(resolved_root);
  if (asset_files.empty()) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Warning,
                    resolved_root.generic_string(), "skill.bundle.no_internal_assets",
                    "plugin skill asset root does not contain normalized .skill.yaml files");
    return assets;
  }

  for (const auto& asset_file : asset_files) {
    const auto document = parse_key_value_yaml_file(asset_file);
    if (!document.ok) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      asset_file.generic_string(), "skill.bundle.asset_invalid",
                      document.error.empty() ? "normalized skill yaml could not be parsed"
                                             : document.error);
      continue;
    }

    const auto workflow_template_ref = pick_scalar(document, "workflow_template_ref");
    const auto eval_suite_ref = pick_scalar(document, "eval_suite_ref");
    if (!workflow_template_ref.has_value() || !eval_suite_ref.has_value()) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      asset_file.generic_string(), "skill.bundle.asset_invalid",
                      "normalized skill yaml must declare workflow_template_ref and eval_suite_ref");
      continue;
    }

    const auto workflow_path = resolve_import_path(
        options_.project_root, resolved_root, std::filesystem::path(*workflow_template_ref));
    const auto eval_path = resolve_import_path(
        options_.project_root, resolved_root, std::filesystem::path(*eval_suite_ref));
    if (!std::filesystem::exists(workflow_path)) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      workflow_path.generic_string(), "skill.bundle.workflow_missing",
                      "normalized skill yaml references a missing workflow template");
      continue;
    }
    if (!std::filesystem::exists(eval_path)) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      eval_path.generic_string(), "skill.bundle.eval_missing",
                      "normalized skill yaml references a missing eval suite");
      continue;
    }

    SkillSpecAsset asset{
        .skill_id = pick_scalar(document, "skill_id").value_or(std::string()),
        .source_key = skill_asset_ref.source_key,
        .asset_ref = make_asset_ref(options_.project_root, asset_file),
        .version = pick_scalar(document, "version").value_or(std::string()),
        .name = pick_scalar(document, "name").value_or(std::string()),
        .description = pick_scalar(document, "description").value_or(std::string()),
        .intent_patterns = pick_list(document, "intent_patterns"),
        .tags = pick_list(document, "tags"),
        .allowed_tools = pick_list(document, "allowed_tools"),
        .profile_constraints = pick_list(document, "profile_constraints"),
        .required_domains = pick_list(document, "required_domains"),
        .workflow_template_ref = make_asset_ref(options_.project_root, workflow_path),
        .prompt_bundle_ref = pick_scalar(document, "prompt_bundle_ref"),
        .eval_suite_ref = make_asset_ref(options_.project_root, eval_path),
        .fallback_mode = pick_scalar(document, "fallback_mode").value_or(std::string()),
    };

    if (!asset.has_consistent_values()) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      asset_file.generic_string(), "skill.bundle.asset_invalid",
                      "normalized skill asset is incomplete or inconsistent");
      continue;
    }

    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Info,
                    asset_file.generic_string(), "skill.bundle.imported",
                    "plugin-delivered normalized skill asset was imported successfully");
    assets.push_back(std::move(asset));
  }

  return assets;
}

std::filesystem::path PluginSkillBundleImporter::resolve_path(
    const std::filesystem::path& path) const {
  if (path.is_absolute()) {
    return path.lexically_normal();
  }

  return (options_.project_root / path).lexically_normal();
}

}  // namespace dasall::tools::skills