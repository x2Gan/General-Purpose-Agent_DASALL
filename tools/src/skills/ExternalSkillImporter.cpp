#include "skills/ExternalSkillImporter.h"

#include <algorithm>
#include <fstream>
#include <sstream>

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

[[nodiscard]] std::vector<std::filesystem::path> collect_skill_directories(
    const std::filesystem::path& directory_path) {
  std::vector<std::filesystem::path> skill_directories;

  if (std::filesystem::exists(directory_path / "SKILL.md")) {
    skill_directories.push_back(directory_path);
  } else if (std::filesystem::exists(directory_path) &&
             std::filesystem::is_directory(directory_path)) {
    for (const auto& entry : std::filesystem::directory_iterator(directory_path)) {
      if (!entry.is_directory()) {
        continue;
      }

      if (std::filesystem::exists(entry.path() / "SKILL.md")) {
        skill_directories.push_back(entry.path());
      }
    }
  }

  std::sort(skill_directories.begin(), skill_directories.end());
  return skill_directories;
}

[[nodiscard]] std::string first_non_empty_paragraph(std::string_view body) {
  std::istringstream stream{std::string(body)};
  std::string line;
  while (std::getline(stream, line)) {
    line = trim_copy(std::move(line));
    if (!line.empty() && !line.starts_with('#')) {
      return line;
    }
  }

  return "";
}

[[nodiscard]] std::vector<std::string> pick_list(
    const ParsedSkillFrontmatter& parsed,
    std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    const auto found = parsed.list_fields.find(std::string(key));
    if (found != parsed.list_fields.end()) {
      return unique_non_empty_values(found->second);
    }
  }

  return {};
}

[[nodiscard]] std::optional<std::string> pick_scalar(
    const ParsedSkillFrontmatter& parsed,
    std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    const auto found = parsed.scalar_fields.find(std::string(key));
    if (found != parsed.scalar_fields.end() && !found->second.empty()) {
      return found->second;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::vector<std::string> allowed_tools_from_workflow(
    const ParsedKeyValueDocument& workflow_document) {
  const auto allowed_tools = workflow_document.list_values.find("allowed_tools");
  if (allowed_tools != workflow_document.list_values.end()) {
    return unique_non_empty_values(allowed_tools->second);
  }

  const auto tool_allowlist = workflow_document.list_values.find("tool_allowlist");
  if (tool_allowlist != workflow_document.list_values.end()) {
    return unique_non_empty_values(tool_allowlist->second);
  }

  return {};
}

[[nodiscard]] std::vector<std::string> required_domains_from_tools(
    const std::vector<std::string>& allowed_tools) {
  std::vector<std::string> domains;
  domains.reserve(allowed_tools.size());
  for (const auto& tool_name : allowed_tools) {
    const auto delimiter = tool_name.find('.');
    if (delimiter == std::string::npos) {
      domains.push_back(tool_name);
      continue;
    }

    domains.push_back(tool_name.substr(0U, delimiter));
  }

  return unique_non_empty_values(domains);
}

[[nodiscard]] std::string derive_skill_id(
    std::string_view source_key,
    const std::optional<std::string>& dialect_ref,
    std::string_view skill_name) {
  std::string skill_id = normalize_identifier(source_key);
  if (dialect_ref.has_value() && !dialect_is_internal(dialect_ref)) {
    if (!skill_id.empty()) {
      skill_id.append(".");
    }
    skill_id.append(normalize_identifier(*dialect_ref));
  }

  const auto normalized_name = normalize_identifier(skill_name);
  if (!normalized_name.empty()) {
    if (!skill_id.empty()) {
      skill_id.append(".");
    }
    skill_id.append(normalized_name);
  }

  return skill_id;
}

}  // namespace

ExternalSkillImporter::ExternalSkillImporter(SkillImporterOptions options)
    : options_(std::move(options)) {
  if (options_.project_root.empty()) {
    options_.project_root = std::filesystem::current_path();
  }
}

SkillImportResult ExternalSkillImporter::import_directory(
    std::string_view source_key,
    const std::filesystem::path& directory_path,
    std::optional<std::string> dialect_ref) const {
  SkillImportResult result;

  if (source_key.empty()) {
    push_diagnostic(result.diagnostics, SkillImportDiagnosticLevel::Error,
                    directory_path.generic_string(), "skill.importer.source_missing",
                    "source_key must be provided for external skill imports");
    result.diagnostics = emit_diagnostics(std::move(result.diagnostics));
    return result;
  }

  if (!options_.external_skill_import_enabled) {
    push_diagnostic(result.diagnostics, SkillImportDiagnosticLevel::Warning,
                    directory_path.generic_string(), "skill.importer.feature_disabled",
                    "external skill import remains disabled by the module-local feature flag");
    result.diagnostics = emit_diagnostics(std::move(result.diagnostics));
    return result;
  }

  result.imported_assets = normalize_assets(source_key, directory_path, dialect_ref, result.diagnostics);
  result.scanned_entries = collect_skill_directories(resolve_path(directory_path)).size();
  result.diagnostics = emit_diagnostics(std::move(result.diagnostics));
  return result;
}

std::optional<ParsedSkillFrontmatter> ExternalSkillImporter::parse_frontmatter(
    const std::filesystem::path& skill_markdown_path,
    std::vector<SkillImportDiagnostic>& diagnostics) {
  std::ifstream stream(skill_markdown_path);
  if (!stream.is_open()) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                    skill_markdown_path.generic_string(),
                    "skill.importer.skill_markdown_missing",
                    "unable to open SKILL.md for parsing");
    return std::nullopt;
  }

  std::string first_line;
  if (!std::getline(stream, first_line) || trim_copy(first_line) != "---") {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                    skill_markdown_path.generic_string(),
                    "skill.importer.frontmatter_missing",
                    "SKILL.md must begin with a YAML frontmatter block");
    return std::nullopt;
  }

  std::ostringstream frontmatter_buffer;
  std::ostringstream body_buffer;
  std::string line;
  bool found_closing_delimiter = false;
  while (std::getline(stream, line)) {
    if (trim_copy(line) == "---") {
      found_closing_delimiter = true;
      break;
    }
    frontmatter_buffer << line << '\n';
  }

  if (!found_closing_delimiter) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                    skill_markdown_path.generic_string(),
                    "skill.importer.frontmatter_invalid",
                    "frontmatter closing delimiter is missing");
    return std::nullopt;
  }

  while (std::getline(stream, line)) {
    body_buffer << line << '\n';
  }

  const auto parsed_document = parse_key_value_document(frontmatter_buffer.str());
  if (!parsed_document.ok) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                    skill_markdown_path.generic_string(),
                    "skill.importer.frontmatter_invalid",
                    parsed_document.error.empty() ? "unable to parse frontmatter"
                                                  : parsed_document.error);
    return std::nullopt;
  }

  return ParsedSkillFrontmatter{
      .skill_markdown_path = skill_markdown_path,
      .scalar_fields = std::move(parsed_document.scalar_values),
      .list_fields = std::move(parsed_document.list_values),
      .body = body_buffer.str(),
  };
}

std::vector<SkillSpecAsset> ExternalSkillImporter::normalize_assets(
    std::string_view source_key,
    const std::filesystem::path& directory_path,
    const std::optional<std::string>& dialect_ref,
    std::vector<SkillImportDiagnostic>& diagnostics) const {
  std::vector<SkillSpecAsset> assets;
  const auto resolved_directory = resolve_path(directory_path);
  if (!std::filesystem::exists(resolved_directory) ||
      !std::filesystem::is_directory(resolved_directory)) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                    resolved_directory.generic_string(), "skill.importer.directory_missing",
                    "external skill directory does not exist");
    return assets;
  }

  const auto skill_directories = collect_skill_directories(resolved_directory);
  if (skill_directories.empty()) {
    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Warning,
                    resolved_directory.generic_string(), "skill.importer.no_skill_entries",
                    "no SKILL.md entries were found under the requested directory");
    return assets;
  }

  for (const auto& skill_directory : skill_directories) {
    auto parsed_frontmatter = parse_frontmatter(skill_directory / "SKILL.md", diagnostics);
    if (!parsed_frontmatter.has_value()) {
      continue;
    }

    const auto workflow_ref = pick_scalar(
        *parsed_frontmatter, {"workflow-template", "workflow"});
    const auto eval_ref = pick_scalar(
        *parsed_frontmatter, {"eval-suite", "eval_suite"});
    if (!workflow_ref.has_value()) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      (skill_directory / "SKILL.md").generic_string(),
                      "skill.importer.workflow_missing",
                      "frontmatter must declare workflow-template or workflow");
      continue;
    }
    if (!eval_ref.has_value()) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      (skill_directory / "SKILL.md").generic_string(),
                      "skill.importer.eval_missing",
                      "frontmatter must declare eval-suite or eval_suite");
      continue;
    }

    const auto workflow_path = resolve_import_path(
        options_.project_root, skill_directory, std::filesystem::path(*workflow_ref));
    if (!std::filesystem::exists(workflow_path)) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      workflow_path.generic_string(), "skill.importer.workflow_missing",
                      "workflow template referenced by SKILL.md does not exist");
      continue;
    }

    const auto eval_path = resolve_import_path(
        options_.project_root, skill_directory, std::filesystem::path(*eval_ref));
    if (!std::filesystem::exists(eval_path)) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      eval_path.generic_string(), "skill.importer.eval_missing",
                      "eval suite referenced by SKILL.md does not exist");
      continue;
    }

    const auto workflow_document = parse_key_value_yaml_file(workflow_path);
    if (!workflow_document.ok) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      workflow_path.generic_string(), "skill.importer.workflow_invalid",
                      workflow_document.error.empty() ? "workflow yaml could not be parsed"
                                                      : workflow_document.error);
      continue;
    }

    auto allowed_tools = allowed_tools_from_workflow(workflow_document);
    if (allowed_tools.empty()) {
      allowed_tools = pick_list(*parsed_frontmatter, {"tool-allowlist", "allowed-tools"});
    }

    std::vector<std::string> intent_patterns;
    const auto intent_keywords = workflow_document.list_values.find("intent_keywords");
    if (intent_keywords != workflow_document.list_values.end()) {
      intent_patterns = unique_non_empty_values(intent_keywords->second);
    }

    if (intent_patterns.empty()) {
      const auto when_to_use = pick_scalar(*parsed_frontmatter, {"when_to_use"});
      if (when_to_use.has_value()) {
        intent_patterns.push_back(*when_to_use);
      }
    }

    const auto name = pick_scalar(*parsed_frontmatter, {"name"})
                          .value_or(skill_directory.filename().string());
    if (intent_patterns.empty()) {
      intent_patterns.push_back(name);
    }

    const auto description = pick_scalar(*parsed_frontmatter, {"description"})
                                 .value_or(first_non_empty_paragraph(parsed_frontmatter->body));
    const auto fallback_mode = workflow_document.scalar_values.contains("fallback_mode")
                                   ? workflow_document.scalar_values.at("fallback_mode")
                                   : std::string("reject");
    const auto skill_id = derive_skill_id(source_key, dialect_ref, name);

    SkillSpecAsset asset{
        .skill_id = skill_id,
        .source_key = std::string(source_key),
        .asset_ref = make_asset_ref(options_.project_root, parsed_frontmatter->skill_markdown_path),
        .version = "1",
        .name = name,
        .description = description,
        .intent_patterns = unique_non_empty_values(intent_patterns),
        .tags = tokenize_identifier(name),
        .allowed_tools = std::move(allowed_tools),
        .profile_constraints = {},
        .required_domains = {},
        .workflow_template_ref = make_asset_ref(options_.project_root, workflow_path),
        .prompt_bundle_ref = std::nullopt,
        .eval_suite_ref = make_asset_ref(options_.project_root, eval_path),
        .fallback_mode = fallback_mode,
    };
    asset.required_domains = required_domains_from_tools(asset.allowed_tools);

    if (!asset.has_consistent_values()) {
      push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Error,
                      parsed_frontmatter->skill_markdown_path.generic_string(),
                      "skill.importer.asset_invalid",
                      "normalized skill asset is incomplete or inconsistent");
      continue;
    }

    push_diagnostic(diagnostics, SkillImportDiagnosticLevel::Info,
                    parsed_frontmatter->skill_markdown_path.generic_string(),
                    "skill.importer.imported",
                    "external skill bundle was normalized successfully");
    assets.push_back(std::move(asset));
  }

  return assets;
}

std::vector<SkillImportDiagnostic> ExternalSkillImporter::emit_diagnostics(
    std::vector<SkillImportDiagnostic> diagnostics) {
  std::sort(diagnostics.begin(), diagnostics.end(),
            [](const SkillImportDiagnostic& left, const SkillImportDiagnostic& right) {
              if (left.source_ref != right.source_ref) {
                return left.source_ref < right.source_ref;
              }
              if (left.reason_code != right.reason_code) {
                return left.reason_code < right.reason_code;
              }
              return left.message < right.message;
            });
  return diagnostics;
}

std::filesystem::path ExternalSkillImporter::resolve_path(
    const std::filesystem::path& path) const {
  if (path.is_absolute()) {
    return path.lexically_normal();
  }

  return (options_.project_root / path).lexically_normal();
}

}  // namespace dasall::tools::skills