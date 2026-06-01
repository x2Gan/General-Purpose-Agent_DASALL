#include "PromptAssetRepository.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "../asset/KeyValueYamlParser.h"

namespace {

using PromptLoadConfig = dasall::llm::PromptAssetSourceConfig;
using PromptAssetDescriptor = dasall::llm::prompt::PromptAssetDescriptor;
using PromptCatalog = dasall::llm::prompt::PromptCatalog;

struct TextFileResult {
  bool ok = false;
  std::string value;
  std::string error;
};

struct DescriptorLoadResult {
  bool ok = false;
  PromptAssetDescriptor descriptor;
  std::string error;
};

struct CatalogBuildResult {
  bool ok = false;
  PromptCatalog catalog;
  std::string error;
};

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string path_to_string(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

TextFileResult read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    return TextFileResult{
        .ok = false,
        .value = {},
        .error = "unable to open text file",
    };
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return TextFileResult{
      .ok = true,
      .value = buffer.str(),
      .error = {},
  };
}

std::optional<std::string> get_required_scalar(const dasall::llm::detail::ParsedKeyValueYaml& yaml,
                                               const std::string& key,
                                               std::string& error) {
  const auto it = yaml.scalar_values.find(key);
  if (it == yaml.scalar_values.end() || it->second.empty()) {
    error = "missing required manifest field: " + key;
    return std::nullopt;
  }

  return it->second;
}

std::optional<std::vector<std::string>> get_optional_list(
    const dasall::llm::detail::ParsedKeyValueYaml& yaml,
    const std::string& key) {
  const auto it = yaml.list_values.find(key);
  if (it == yaml.list_values.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

std::optional<bool> parse_bool(const std::string& raw_value) {
  const std::string value = to_lower_copy(raw_value);
  if (value == "true" || value == "1" || value == "yes") {
    return true;
  }

  if (value == "false" || value == "0" || value == "no") {
    return false;
  }

  return std::nullopt;
}

std::optional<dasall::contracts::CompositionStage> parse_stage(const std::string& raw_value) {
  const std::string value = to_lower_copy(raw_value);
  if (value == "perception") {
    return dasall::contracts::CompositionStage::Perception;
  }

  if (value == "planning") {
    return dasall::contracts::CompositionStage::Planning;
  }

  if (value == "execution") {
    return dasall::contracts::CompositionStage::Execution;
  }

  if (value == "reflection") {
    return dasall::contracts::CompositionStage::Reflection;
  }

  if (value == "response") {
    return dasall::contracts::CompositionStage::Response;
  }

  return std::nullopt;
}

std::optional<dasall::contracts::PromptEvalStatus> parse_eval_status(const std::string& raw_value) {
  const std::string value = to_lower_copy(raw_value);
  if (value == "draft") {
    return dasall::contracts::PromptEvalStatus::Draft;
  }

  if (value == "experiment") {
    return dasall::contracts::PromptEvalStatus::Experiment;
  }

  if (value == "canary") {
    return dasall::contracts::PromptEvalStatus::Canary;
  }

  if (value == "stable") {
    return dasall::contracts::PromptEvalStatus::Stable;
  }

  if (value == "deprecated") {
    return dasall::contracts::PromptEvalStatus::Deprecated;
  }

  return std::nullopt;
}

std::string compute_content_hash(std::string_view content) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char character : content) {
    hash ^= static_cast<std::uint64_t>(character);
    hash *= 1099511628211ULL;
  }

  std::ostringstream stream;
  stream << std::hex << std::setw(16) << std::setfill('0') << hash;
  return stream.str();
}

std::optional<std::vector<std::string>> load_policy_notes(const std::filesystem::path& path,
                                                          std::string& error) {
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }

  TextFileResult file = read_text_file(path);
  if (!file.ok) {
    error = "failed to read optional policy notes: " + path_to_string(path);
    return std::nullopt;
  }

  std::vector<std::string> notes;
  std::istringstream stream(file.value);
  std::string line;
  while (std::getline(stream, line)) {
    std::string trimmed = dasall::llm::detail::trim_copy(line);
    if (!trimmed.empty()) {
      notes.push_back(std::move(trimmed));
    }
  }

  if (notes.empty()) {
    return std::nullopt;
  }

  return notes;
}

bool append_file_to_hash_material(const std::filesystem::path& path,
                                  std::string& hash_material,
                                  std::string& error) {
  TextFileResult file = read_text_file(path);
  if (!file.ok) {
    error = "failed to read file for hash: " + path_to_string(path);
    return false;
  }

  hash_material.append("\nFILE:");
  hash_material.append(path_to_string(path.filename()));
  hash_material.push_back('\n');
  hash_material.append(file.value);
  return true;
}

DescriptorLoadResult load_prompt_asset_descriptor(const std::filesystem::path& package_root,
                                                  const std::string& source_layer) {
  DescriptorLoadResult result;
  const std::filesystem::path manifest_path = package_root / "manifest.yaml";
  const std::filesystem::path system_path = package_root / "system.md";
  const std::filesystem::path task_path = package_root / "task.md";
  const std::filesystem::path policy_notes_path = package_root / "policy_notes.md";

  const auto manifest = dasall::llm::detail::parse_key_value_yaml_file(manifest_path);
  if (!manifest.ok) {
    result.error = "failed to parse prompt manifest " + path_to_string(manifest_path) + ": " +
                   manifest.error;
    return result;
  }

  std::string error;
  const auto schema_version = get_required_scalar(manifest, "schema_version", error);
  if (!schema_version.has_value()) {
    result.error = error;
    return result;
  }

  const auto min_loader_version = get_required_scalar(manifest, "min_loader_version", error);
  if (!min_loader_version.has_value()) {
    result.error = error;
    return result;
  }

  if (*schema_version != "1") {
    result.error = "unsupported prompt schema_version: " + *schema_version;
    return result;
  }

  if (*min_loader_version != "1") {
    result.error = "unsupported prompt min_loader_version: " + *min_loader_version;
    return result;
  }

  const auto prompt_id = get_required_scalar(manifest, "prompt_id", error);
  if (!prompt_id.has_value()) {
    result.error = error;
    return result;
  }

  const auto version = get_required_scalar(manifest, "version", error);
  if (!version.has_value()) {
    result.error = error;
    return result;
  }

  const auto stage_value = get_required_scalar(manifest, "stage", error);
  if (!stage_value.has_value()) {
    result.error = error;
    return result;
  }

  const auto eval_status_value = get_required_scalar(manifest, "eval_status", error);
  if (!eval_status_value.has_value()) {
    result.error = error;
    return result;
  }

  const auto release_scope = get_required_scalar(manifest, "release_scope", error);
  if (!release_scope.has_value()) {
    result.error = error;
    return result;
  }

  const auto output_schema_ref = get_required_scalar(manifest, "output_schema_ref", error);
  if (!output_schema_ref.has_value()) {
    result.error = error;
    return result;
  }

  const auto trusted_source = get_required_scalar(manifest, "trusted_source", error);
  if (!trusted_source.has_value()) {
    result.error = error;
    return result;
  }

  const auto tags = get_optional_list(manifest, "tags");
  if (!tags.has_value() || tags->empty()) {
    result.error = "missing required manifest field: tags";
    return result;
  }

  const auto parsed_stage = parse_stage(*stage_value);
  if (!parsed_stage.has_value()) {
    result.error = "unsupported prompt stage: " + *stage_value;
    return result;
  }

  const auto parsed_eval_status = parse_eval_status(*eval_status_value);
  if (!parsed_eval_status.has_value()) {
    result.error = "unsupported prompt eval_status: " + *eval_status_value;
    return result;
  }

  TextFileResult system_text = read_text_file(system_path);
  if (!system_text.ok) {
    result.error = "missing required prompt body: " + path_to_string(system_path);
    return result;
  }

  TextFileResult task_text = read_text_file(task_path);
  if (!task_text.ok) {
    result.error = "missing required prompt body: " + path_to_string(task_path);
    return result;
  }

  std::optional<std::vector<std::string>> policy_notes = load_policy_notes(policy_notes_path, error);
  if (!error.empty()) {
    result.error = error;
    return result;
  }

  const auto few_shot_refs = get_optional_list(manifest, "few_shot_refs");
  if (few_shot_refs.has_value()) {
    for (const auto& ref : *few_shot_refs) {
      const std::filesystem::path few_shot_path = package_root / ref;
      if (!std::filesystem::exists(few_shot_path)) {
        result.error = "missing referenced few-shot asset: " + path_to_string(few_shot_path);
        return result;
      }
    }
  }

  std::string hash_material;
  if (!append_file_to_hash_material(manifest_path, hash_material, error) ||
      !append_file_to_hash_material(system_path, hash_material, error) ||
      !append_file_to_hash_material(task_path, hash_material, error)) {
    result.error = error;
    return result;
  }

  if (std::filesystem::exists(policy_notes_path) &&
      !append_file_to_hash_material(policy_notes_path, hash_material, error)) {
    result.error = error;
    return result;
  }

  if (few_shot_refs.has_value()) {
    for (const auto& ref : *few_shot_refs) {
      if (!append_file_to_hash_material(package_root / ref, hash_material, error)) {
        result.error = error;
        return result;
      }
    }
  }

  PromptAssetDescriptor descriptor;
  descriptor.package_id = manifest.scalar_values.contains("package_id")
                              ? manifest.scalar_values.at("package_id")
                              : *prompt_id + "." + *version;
  descriptor.schema_version = *schema_version;
  descriptor.min_loader_version = *min_loader_version;
  descriptor.source_layer = source_layer;
  descriptor.source_uri = manifest.scalar_values.contains("source_uri")
                              ? manifest.scalar_values.at("source_uri")
                              : path_to_string(package_root);
  descriptor.content_hash = compute_content_hash(hash_material);
  descriptor.scene_id = manifest.scalar_values.contains("scene_id")
                            ? manifest.scalar_values.at("scene_id")
                            : std::string();
  descriptor.persona_id = manifest.scalar_values.contains("persona_id")
                              ? manifest.scalar_values.at("persona_id")
                              : std::string();
  descriptor.profile_tags = get_optional_list(manifest, "profile_tags").value_or(
      std::vector<std::string>{});
  descriptor.is_default_release = false;
  if (manifest.scalar_values.contains("default_release")) {
    const auto parsed_default = parse_bool(manifest.scalar_values.at("default_release"));
    if (!parsed_default.has_value()) {
      result.error = "invalid default_release value in " + path_to_string(manifest_path);
      return result;
    }

    descriptor.is_default_release = *parsed_default;
  }

  descriptor.spec.prompt_id = *prompt_id;
  descriptor.spec.stage = *parsed_stage;
  descriptor.spec.template_slots = get_optional_list(manifest, "template_slots");
  descriptor.spec.task_types = get_optional_list(manifest, "task_types");
  if (manifest.scalar_values.contains("language")) {
    descriptor.spec.language = manifest.scalar_values.at("language");
  }
  if (manifest.scalar_values.contains("model_family")) {
    descriptor.spec.model_family = manifest.scalar_values.at("model_family");
  }
  descriptor.spec.output_schema_ref = *output_schema_ref;
  descriptor.spec.tool_hints = get_optional_list(manifest, "tool_hints");
  descriptor.spec.tags = *tags;

  descriptor.release.prompt_id = *prompt_id;
  descriptor.release.version = *version;
  descriptor.release.stage = *parsed_stage;
  descriptor.release.eval_status = *parsed_eval_status;
  descriptor.release.release_scope = *release_scope;
  descriptor.release.system_instructions = system_text.value;
  descriptor.release.task_template = task_text.value;
  descriptor.release.output_schema_ref = *output_schema_ref;
  descriptor.release.few_shot_refs = few_shot_refs;
  descriptor.release.policy_notes = policy_notes;
  if (manifest.scalar_values.contains("rollback_from")) {
    descriptor.release.rollback_from = manifest.scalar_values.at("rollback_from");
  }
  descriptor.release.trusted_source = *trusted_source;
  descriptor.release.tags = *tags;

  if (!descriptor.has_consistent_values()) {
    result.error = "prompt asset descriptor failed consistency checks for " +
                   path_to_string(package_root);
    return result;
  }

  result.ok = true;
  result.descriptor = std::move(descriptor);
  return result;
}

void upsert_descriptor(PromptCatalog& catalog, PromptAssetDescriptor descriptor) {
  auto existing = std::find_if(catalog.descriptors.begin(), catalog.descriptors.end(),
                               [&](const PromptAssetDescriptor& candidate) {
                                 return candidate.release.prompt_id == descriptor.release.prompt_id &&
                                        candidate.release.version == descriptor.release.version;
                               });

  if (existing == catalog.descriptors.end()) {
    catalog.descriptors.push_back(std::move(descriptor));
    return;
  }

  *existing = std::move(descriptor);
}

CatalogBuildResult load_layer_catalog(const std::filesystem::path& root,
                                      const std::string& source_layer,
                                      bool require_existing_root) {
  CatalogBuildResult result;

  if (root.empty()) {
    result.ok = true;
    return result;
  }

  if (!std::filesystem::exists(root)) {
    if (require_existing_root) {
      result.error = "prompt asset root does not exist: " + path_to_string(root);
      return result;
    }

    result.ok = true;
    return result;
  }

  if (!std::filesystem::is_directory(root)) {
    result.error = "prompt asset root is not a directory: " + path_to_string(root);
    return result;
  }

  std::vector<std::filesystem::path> manifest_paths;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().filename() == "manifest.yaml") {
      manifest_paths.push_back(entry.path());
    }
  }

  std::sort(manifest_paths.begin(), manifest_paths.end());
  for (const auto& manifest_path : manifest_paths) {
    DescriptorLoadResult descriptor =
        load_prompt_asset_descriptor(manifest_path.parent_path(), source_layer);
    if (!descriptor.ok) {
      result.error = descriptor.error;
      return result;
    }

    upsert_descriptor(result.catalog, std::move(descriptor.descriptor));
  }

  result.ok = true;
  return result;
}

CatalogBuildResult build_prompt_catalog(const PromptLoadConfig& config) {
  CatalogBuildResult merged = load_layer_catalog(config.baseline_root, "baseline", true);
  if (!merged.ok) {
    return merged;
  }

  CatalogBuildResult deployment = load_layer_catalog(config.deployment_root, "deployment", false);
  if (!deployment.ok) {
    return deployment;
  }

  for (auto& descriptor : deployment.catalog.descriptors) {
    upsert_descriptor(merged.catalog, std::move(descriptor));
  }

  CatalogBuildResult snapshot = load_layer_catalog(config.snapshot_cache_root, "snapshot", false);
  if (!snapshot.ok) {
    return snapshot;
  }

  for (auto& descriptor : snapshot.catalog.descriptors) {
    upsert_descriptor(merged.catalog, std::move(descriptor));
  }

  if (merged.catalog.descriptors.empty()) {
    merged.error = "prompt catalog contains no loadable packages";
    merged.ok = false;
    return merged;
  }

  if (!merged.catalog.has_consistent_values()) {
    merged.error = "prompt catalog failed consistency checks";
    merged.ok = false;
    return merged;
  }

  merged.ok = true;
  return merged;
}

}  // namespace

namespace dasall::llm::prompt {

bool PromptCatalog::has_consistent_values() const {
  return !descriptors.empty() &&
         std::all_of(descriptors.begin(), descriptors.end(), [](const PromptAssetDescriptor& item) {
           return item.has_consistent_values();
         });
}

const PromptAssetDescriptor* PromptCatalog::find_release(std::string_view prompt_id,
                                                         std::string_view version) const {
  const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                               [&](const PromptAssetDescriptor& item) {
                                 return item.release.prompt_id.has_value() &&
                                        item.release.version.has_value() &&
                                        *item.release.prompt_id == prompt_id &&
                                        *item.release.version == version;
                               });
  if (it == descriptors.end()) {
    return nullptr;
  }

  return &(*it);
}

bool PromptAssetRepository::init(const PromptAssetSourceConfig& config) {
  if (!config.has_consistent_values()) {
    last_error_message_ = "prompt asset repository received inconsistent source config";
    return false;
  }

  config_ = config;
  initialized_ = true;
  return reload();
}

bool PromptAssetRepository::reload() {
  if (!initialized_) {
    last_error_message_ = "prompt asset repository has not been initialized";
    return false;
  }

  CatalogBuildResult candidate = build_prompt_catalog(config_);
  if (!candidate.ok) {
    last_error_message_ = candidate.error;
    return false;
  }

  auto snapshot = std::make_shared<const PromptCatalog>(std::move(candidate.catalog));
  std::atomic_store_explicit(&catalog_snapshot_, snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

std::shared_ptr<const PromptCatalog> PromptAssetRepository::snapshot() const {
  return std::atomic_load_explicit(&catalog_snapshot_, std::memory_order_acquire);
}

std::string PromptAssetRepository::last_error_message() const {
  return last_error_message_;
}

}  // namespace dasall::llm::prompt