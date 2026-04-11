#include "ProviderCatalogRepository.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "../asset/KeyValueYamlParser.h"

namespace {

using CatalogConfig = dasall::llm::ProviderCatalogSourceConfig;
using ParsedKeyValueYaml = dasall::llm::detail::ParsedKeyValueYaml;
using ProviderCatalogProvider = dasall::llm::provider::ProviderCatalogProvider;
using ProviderCatalogRepository = dasall::llm::provider::ProviderCatalogRepository;
using ProviderCatalogSnapshot = dasall::llm::provider::ProviderCatalogSnapshot;
using ProviderModelMetadata = dasall::llm::provider::ProviderModelMetadata;
using ProviderRuntimeSettings = dasall::llm::provider::ProviderRuntimeSettings;

struct CatalogIndex {
  std::string default_source_version;
  std::vector<std::string> packages;
};

struct CatalogIndexResult {
  bool ok = false;
  CatalogIndex index;
  std::string error;
};

struct ProviderPackage {
  ProviderCatalogProvider provider;
  std::vector<ProviderModelMetadata> models;
};

struct ProviderPackageLoadResult {
  bool ok = false;
  ProviderPackage package;
  std::string error;
};

struct LayerCatalog {
  std::string default_source_version;
  std::vector<ProviderPackage> packages;
};

struct LayerCatalogBuildResult {
  bool ok = false;
  LayerCatalog layer;
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

std::optional<std::string> get_required_scalar(const ParsedKeyValueYaml& yaml,
                                               const std::string& key,
                                               std::string& error) {
  const auto it = yaml.scalar_values.find(key);
  if (it == yaml.scalar_values.end() || it->second.empty()) {
    error = "missing required field: " + key;
    return std::nullopt;
  }

  return it->second;
}

std::optional<std::string> get_optional_scalar(const ParsedKeyValueYaml& yaml,
                                               const std::string& key) {
  const auto it = yaml.scalar_values.find(key);
  if (it == yaml.scalar_values.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

std::optional<std::vector<std::string>> get_optional_list(const ParsedKeyValueYaml& yaml,
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

std::optional<std::uint32_t> parse_u32(const std::string& raw_value) {
  try {
    return static_cast<std::uint32_t>(std::stoul(raw_value));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<double> parse_double(const std::string& raw_value) {
  try {
    return std::stod(raw_value);
  } catch (...) {
    return std::nullopt;
  }
}

bool is_reference_value(const std::string& raw_value) {
  return raw_value.starts_with("secret://") || raw_value.starts_with("profile://") ||
         raw_value.starts_with("header://");
}

std::vector<std::string> merge_unique_vectors(std::vector<std::string> left,
                                              const std::vector<std::string>& right) {
  for (const auto& value : right) {
    if (std::find(left.begin(), left.end(), value) == left.end()) {
      left.push_back(value);
    }
  }

  return left;
}

std::optional<std::string> get_required_prefixed_scalar(const ParsedKeyValueYaml& yaml,
                                                        const std::string& prefix,
                                                        const std::string& key,
                                                        std::string& error) {
  return get_required_scalar(yaml, prefix + key, error);
}

std::optional<std::vector<std::string>> get_optional_prefixed_list(const ParsedKeyValueYaml& yaml,
                                                                   const std::string& prefix,
                                                                   const std::string& key) {
  return get_optional_list(yaml, prefix + key);
}

std::vector<std::string> collect_model_keys(const ParsedKeyValueYaml& yaml) {
  std::set<std::string> model_keys;

  const auto collect = [&](const auto& container) {
    for (const auto& [key, value] : container) {
      static_cast<void>(value);
      if (!key.starts_with("models.")) {
        continue;
      }

      const std::string remainder = key.substr(std::string("models.").size());
      const auto separator = remainder.find('.');
      if (separator == std::string::npos || separator == 0U) {
        continue;
      }

      model_keys.insert(remainder.substr(0U, separator));
    }
  };

  collect(yaml.scalar_values);
  collect(yaml.list_values);
  return std::vector<std::string>(model_keys.begin(), model_keys.end());
}

std::unordered_map<std::string, std::string> collect_prefixed_map(const ParsedKeyValueYaml& yaml,
                                                                  const std::string& prefix) {
  std::unordered_map<std::string, std::string> values;
  for (const auto& [key, value] : yaml.scalar_values) {
    if (!key.starts_with(prefix)) {
      continue;
    }

    values.emplace(key.substr(prefix.size()), value);
  }

  return values;
}

std::string aggregate_verification_state(
    const std::unordered_map<std::string, std::string>& verification_states) {
  bool saw_limited = false;
  for (const auto& [feature, raw_state] : verification_states) {
    static_cast<void>(feature);
    const std::string state = to_lower_copy(raw_state);
    if (state == "blocked") {
      return "blocked";
    }

    if (state == "limited" || state == "declared" || state == "needs_integration_validation") {
      saw_limited = true;
    }
  }

  return saw_limited ? "limited" : "verified";
}

CatalogIndexResult load_catalog_index(const std::filesystem::path& root,
                                      bool require_existing_root) {
  CatalogIndexResult result;
  if (root.empty()) {
    result.ok = true;
    return result;
  }

  if (!std::filesystem::exists(root)) {
    if (require_existing_root) {
      result.error = "provider catalog root does not exist: " + path_to_string(root);
      return result;
    }

    result.ok = true;
    return result;
  }

  if (!std::filesystem::is_directory(root)) {
    result.error = "provider catalog root is not a directory: " + path_to_string(root);
    return result;
  }

  const std::filesystem::path catalog_path = root / "catalog.yaml";
  const ParsedKeyValueYaml yaml = dasall::llm::detail::parse_key_value_yaml_file(catalog_path);
  if (!yaml.ok) {
    result.error = "failed to parse provider catalog index " + path_to_string(catalog_path) +
                   ": " + yaml.error;
    return result;
  }

  std::string error;
  const auto schema_version = get_required_scalar(yaml, "schema_version", error);
  if (!schema_version.has_value()) {
    result.error = error;
    return result;
  }

  if (*schema_version != "1") {
    result.error = "unsupported provider catalog schema_version: " + *schema_version;
    return result;
  }

  const auto packages = get_optional_list(yaml, "packages");
  if (packages.has_value()) {
    result.index.packages = *packages;
  } else {
    const auto providers = get_optional_list(yaml, "providers");
    if (providers.has_value()) {
      result.index.packages = *providers;
    }
  }

  result.index.default_source_version = get_optional_scalar(yaml, "default_source_version").value_or(
      std::string{});
  result.ok = true;
  return result;
}

ProviderPackageLoadResult load_provider_package(const std::filesystem::path& package_root,
                                                const std::string& source_layer,
                                                const std::string& default_source_version) {
  ProviderPackageLoadResult result;
  const std::filesystem::path manifest_path = package_root / "manifest.yaml";
  const std::filesystem::path models_path = package_root / "models.yaml";

  const ParsedKeyValueYaml manifest = dasall::llm::detail::parse_key_value_yaml_file(manifest_path);
  if (!manifest.ok) {
    result.error = "failed to parse provider manifest " + path_to_string(manifest_path) + ": " +
                   manifest.error;
    return result;
  }

  const ParsedKeyValueYaml models = dasall::llm::detail::parse_key_value_yaml_file(models_path);
  if (!models.ok) {
    result.error = "failed to parse provider model catalog " + path_to_string(models_path) + ": " +
                   models.error;
    return result;
  }

  std::string error;
  const auto manifest_schema = get_required_scalar(manifest, "schema_version", error);
  if (!manifest_schema.has_value()) {
    result.error = error;
    return result;
  }

  if (*manifest_schema != "1") {
    result.error = "unsupported provider manifest schema_version: " + *manifest_schema;
    return result;
  }

  const auto models_schema = get_required_scalar(models, "schema_version", error);
  if (!models_schema.has_value()) {
    result.error = error;
    return result;
  }

  if (*models_schema != "1") {
    result.error = "unsupported provider models schema_version: " + *models_schema;
    return result;
  }

  const auto provider_id = get_required_scalar(manifest, "provider_id", error);
  const auto display_name = get_required_scalar(manifest, "display_name", error);
  const auto adapter_family = get_required_scalar(manifest, "adapter_family", error);
  const auto api_family = get_required_scalar(manifest, "api_family", error);
  const auto base_url = get_required_scalar(manifest, "base_url", error);
  const auto auth_mode = get_required_scalar(manifest, "auth_mode", error);
  const auto auth_ref = get_required_scalar(manifest, "auth_ref", error);
  const auto trusted_source = get_required_scalar(manifest, "trusted_source", error);
  if (!provider_id.has_value() || !display_name.has_value() || !adapter_family.has_value() ||
      !api_family.has_value() || !base_url.has_value() || !auth_mode.has_value() ||
      !auth_ref.has_value() || !trusted_source.has_value()) {
    result.error = error;
    return result;
  }

  std::string source_version = get_optional_scalar(manifest, "source_version").value_or(
      default_source_version);
  if (source_version.empty()) {
    result.error = "missing required field: source_version";
    return result;
  }

  if (!is_reference_value(*auth_ref) || auth_ref->starts_with("header://")) {
    result.error = "auth_ref must be a secret:// or profile:// reference";
    return result;
  }

  const std::vector<std::string> header_refs =
      get_optional_list(manifest, "header_refs").value_or(std::vector<std::string>{});
  for (const auto& header_ref : header_refs) {
    if (!is_reference_value(header_ref)) {
      result.error = "header_refs must contain reference values only";
      return result;
    }
  }

  const std::vector<std::string> tags =
      get_optional_list(manifest, "tags").value_or(std::vector<std::string>{});
  if (tags.empty()) {
    result.error = "missing required field: tags";
    return result;
  }

  const std::vector<std::string> mutable_overlay_fields =
      get_optional_list(manifest, "mutable_overlay_fields").value_or(std::vector<std::string>{});
  static const std::unordered_set<std::string> kAllowedMutableFields = {
      "auth_ref", "header_refs", "base_url_alias", "activation_flag", "source_version"};
  for (const auto& field : mutable_overlay_fields) {
    if (!kAllowedMutableFields.contains(field)) {
      result.error = "unsupported mutable overlay field: " + field;
      return result;
    }
  }

  bool activation_flag = true;
  if (const auto activation = get_optional_scalar(manifest, "activation_flag");
      activation.has_value()) {
    const auto parsed = parse_bool(*activation);
    if (!parsed.has_value()) {
      result.error = "invalid activation_flag value in " + path_to_string(manifest_path);
      return result;
    }

    activation_flag = *parsed;
  }

  ProviderCatalogProvider provider;
  provider.descriptor.provider_id = *provider_id;
  provider.descriptor.adapter_family = *adapter_family;
  provider.descriptor.api_family = *api_family;
  provider.descriptor.base_url = *base_url;
  provider.descriptor.auth_ref = *auth_ref;
  provider.descriptor.header_refs = header_refs;
  provider.descriptor.capability_tags = tags;
  provider.descriptor.source_version = source_version;
  provider.runtime.display_name = *display_name;
  provider.runtime.auth_mode = *auth_mode;
  provider.runtime.trusted_source = *trusted_source;
  provider.runtime.source_layer = source_layer;
  provider.runtime.base_url_alias =
      get_optional_scalar(manifest, "base_url_alias").value_or(*base_url);
  provider.runtime.tags = tags;
  provider.runtime.mutable_overlay_fields = mutable_overlay_fields;
  provider.runtime.activation_flag = activation_flag;

  const std::vector<std::string> model_keys = collect_model_keys(models);
  if (model_keys.empty()) {
    result.error = "provider package contains no models: " + path_to_string(models_path);
    return result;
  }

  for (const auto& model_key : model_keys) {
    const std::string prefix = "models." + model_key + ".";

    const auto model_id = get_required_prefixed_scalar(models, prefix, "id", error);
    const auto model_display_name = get_required_prefixed_scalar(models, prefix, "display_name", error);
    const auto model_version = get_required_prefixed_scalar(models, prefix, "model_version", error);
    const auto tier_family = get_required_prefixed_scalar(models, prefix, "tier_family", error);
    const auto latency_tier = get_required_prefixed_scalar(models, prefix, "latency_tier", error);
    const auto cost_tier = get_required_prefixed_scalar(models, prefix, "cost_tier", error);
    const auto reasoning_depth_tier =
        get_required_prefixed_scalar(models, prefix, "reasoning_depth_tier", error);
    const auto context_window = get_required_prefixed_scalar(models, prefix, "context_window", error);
    const auto default_max_output_tokens =
        get_required_prefixed_scalar(models, prefix, "default_max_output_tokens", error);
    const auto max_output_tokens_hard_limit =
        get_required_prefixed_scalar(models, prefix, "max_output_tokens_hard_limit", error);
    const auto metadata_source_uri =
        get_required_prefixed_scalar(models, prefix, "metadata_source_uri", error);
    const auto metadata_effective_at =
        get_required_prefixed_scalar(models, prefix, "metadata_effective_at", error);
    const auto pricing_ref = get_required_prefixed_scalar(models, prefix, "pricing.pricing_ref", error);
    const auto input_cache_hit_usd_per_1m =
        get_required_prefixed_scalar(models, prefix, "pricing.input_cache_hit_usd_per_1m", error);
    const auto input_cache_miss_usd_per_1m =
        get_required_prefixed_scalar(models, prefix, "pricing.input_cache_miss_usd_per_1m", error);
    const auto output_usd_per_1m =
        get_required_prefixed_scalar(models, prefix, "pricing.output_usd_per_1m", error);
    const auto supports_tools = get_required_prefixed_scalar(models, prefix, "supports_tools", error);
    const auto supports_reasoning =
        get_required_prefixed_scalar(models, prefix, "supports_reasoning", error);
    const auto supports_visible_reasoning =
        get_required_prefixed_scalar(models, prefix, "supports_visible_reasoning", error);
    const auto supports_prompt_cache =
        get_required_prefixed_scalar(models, prefix, "supports_prompt_cache", error);

    if (!model_id.has_value() || !model_display_name.has_value() || !model_version.has_value() ||
        !tier_family.has_value() || !latency_tier.has_value() || !cost_tier.has_value() ||
        !reasoning_depth_tier.has_value() || !context_window.has_value() ||
        !default_max_output_tokens.has_value() || !max_output_tokens_hard_limit.has_value() ||
        !metadata_source_uri.has_value() || !metadata_effective_at.has_value() ||
        !pricing_ref.has_value() || !input_cache_hit_usd_per_1m.has_value() ||
        !input_cache_miss_usd_per_1m.has_value() || !output_usd_per_1m.has_value() ||
        !supports_tools.has_value() || !supports_reasoning.has_value() ||
        !supports_visible_reasoning.has_value() || !supports_prompt_cache.has_value()) {
      result.error = error;
      return result;
    }

    const auto parsed_context_window = parse_u32(*context_window);
    const auto parsed_default_max_output_tokens = parse_u32(*default_max_output_tokens);
    const auto parsed_hard_limit = parse_u32(*max_output_tokens_hard_limit);
    const auto parsed_input_cache_hit = parse_double(*input_cache_hit_usd_per_1m);
    const auto parsed_input_cache_miss = parse_double(*input_cache_miss_usd_per_1m);
    const auto parsed_output_cost = parse_double(*output_usd_per_1m);
    const auto parsed_supports_tools = parse_bool(*supports_tools);
    const auto parsed_supports_reasoning = parse_bool(*supports_reasoning);
    const auto parsed_supports_visible_reasoning = parse_bool(*supports_visible_reasoning);
    const auto parsed_supports_prompt_cache = parse_bool(*supports_prompt_cache);
    if (!parsed_context_window.has_value() || !parsed_default_max_output_tokens.has_value() ||
        !parsed_hard_limit.has_value() || !parsed_input_cache_hit.has_value() ||
        !parsed_input_cache_miss.has_value() || !parsed_output_cost.has_value() ||
        !parsed_supports_tools.has_value() || !parsed_supports_reasoning.has_value() ||
        !parsed_supports_visible_reasoning.has_value() ||
        !parsed_supports_prompt_cache.has_value()) {
      result.error = "provider model catalog contains an invalid numeric or boolean field";
      return result;
    }

    const auto aliases = get_optional_prefixed_list(models, prefix, "aliases");
    const auto input_modalities = get_optional_prefixed_list(models, prefix, "input_modalities");
    if (!aliases.has_value() || aliases->empty() || !input_modalities.has_value() ||
        input_modalities->empty()) {
      result.error = "provider model catalog is missing aliases or input_modalities";
      return result;
    }

    const auto parsed_supports_streaming =
        parse_bool(get_optional_scalar(models, prefix + "supports_streaming").value_or("false"));
    const auto parsed_supports_json_object =
        parse_bool(get_optional_scalar(models, prefix + "supports_json_object").value_or("false"));
    const auto parsed_supports_json_schema =
        parse_bool(get_optional_scalar(models, prefix + "supports_json_schema").value_or("false"));
    const auto parsed_supports_native_stream_usage = parse_bool(
        get_optional_scalar(models, prefix + "supports_native_stream_usage").value_or("false"));
    if (!parsed_supports_streaming.has_value() || !parsed_supports_json_object.has_value() ||
        !parsed_supports_json_schema.has_value() ||
        !parsed_supports_native_stream_usage.has_value()) {
      result.error = "provider model catalog contains invalid optional boolean fields";
      return result;
    }

    const auto verification_states =
        collect_prefixed_map(models, prefix + "verification_state.");
    if (verification_states.empty()) {
      result.error = "provider model catalog is missing verification_state entries";
      return result;
    }

    ProviderModelMetadata model;
    model.summary.provider_id = *provider_id;
    model.summary.model_id = *model_id;
    model.summary.model_version = *model_version;
    model.summary.tier_family = *tier_family;
    model.summary.latency_tier = *latency_tier;
    model.summary.cost_tier = *cost_tier;
    model.summary.reasoning_depth_tier = *reasoning_depth_tier;
    model.summary.context_window = *parsed_context_window;
    model.summary.default_max_output_tokens = *parsed_default_max_output_tokens;
    model.summary.max_output_tokens_hard_limit = *parsed_hard_limit;
    model.summary.supports_tools = *parsed_supports_tools;
    model.summary.supports_reasoning = *parsed_supports_reasoning;
    model.summary.supports_visible_reasoning = *parsed_supports_visible_reasoning;
    model.summary.supports_prompt_cache = *parsed_supports_prompt_cache;
    model.summary.input_cache_hit_usd_per_1m = *parsed_input_cache_hit;
    model.summary.input_cache_miss_usd_per_1m = *parsed_input_cache_miss;
    model.summary.output_usd_per_1m = *parsed_output_cost;
    model.summary.metadata_source_uri = *metadata_source_uri;
    model.summary.metadata_effective_at = *metadata_effective_at;
    model.summary.verification_state = aggregate_verification_state(verification_states);
    model.display_name = *model_display_name;
    model.reasoning_mode = get_optional_scalar(models, prefix + "reasoning_mode").value_or(
        std::string{});
    model.source_layer = source_layer;
    model.pricing_ref = *pricing_ref;
    model.aliases = *aliases;
    model.input_modalities = *input_modalities;
    model.feature_notes = get_optional_prefixed_list(models, prefix, "feature_notes").value_or(
        std::vector<std::string>{});
    model.response_private_fields =
        get_optional_prefixed_list(models, prefix, "response_private_fields").value_or(
            std::vector<std::string>{});
    model.verification_states = verification_states;
    model.supports_streaming = *parsed_supports_streaming;
    model.supports_json_object = *parsed_supports_json_object;
    model.supports_json_schema = *parsed_supports_json_schema;
    model.supports_native_stream_usage = *parsed_supports_native_stream_usage;
    result.package.models.push_back(std::move(model));
  }

  result.package.provider = std::move(provider);
  result.ok = true;
  return result;
}

bool same_provider_static_fields(const ProviderCatalogProvider& left,
                                 const ProviderCatalogProvider& right) {
  return left.descriptor.provider_id == right.descriptor.provider_id &&
         left.descriptor.adapter_family == right.descriptor.adapter_family &&
         left.descriptor.api_family == right.descriptor.api_family &&
         left.descriptor.base_url == right.descriptor.base_url &&
         left.descriptor.capability_tags == right.descriptor.capability_tags &&
         left.runtime.display_name == right.runtime.display_name &&
         left.runtime.auth_mode == right.runtime.auth_mode &&
         left.runtime.trusted_source == right.runtime.trusted_source &&
         left.runtime.tags == right.runtime.tags;
}

bool same_model_static_fields(const ProviderModelMetadata& left,
                              const ProviderModelMetadata& right) {
  return left.summary.provider_id == right.summary.provider_id &&
         left.summary.model_id == right.summary.model_id &&
         left.summary.model_version == right.summary.model_version &&
         left.summary.tier_family == right.summary.tier_family &&
         left.summary.latency_tier == right.summary.latency_tier &&
         left.summary.cost_tier == right.summary.cost_tier &&
         left.summary.reasoning_depth_tier == right.summary.reasoning_depth_tier &&
         left.summary.context_window == right.summary.context_window &&
         left.summary.default_max_output_tokens == right.summary.default_max_output_tokens &&
         left.summary.max_output_tokens_hard_limit == right.summary.max_output_tokens_hard_limit &&
         left.summary.supports_tools == right.summary.supports_tools &&
         left.summary.supports_reasoning == right.summary.supports_reasoning &&
         left.summary.supports_visible_reasoning == right.summary.supports_visible_reasoning &&
         left.summary.supports_prompt_cache == right.summary.supports_prompt_cache &&
         left.summary.input_cache_hit_usd_per_1m == right.summary.input_cache_hit_usd_per_1m &&
         left.summary.input_cache_miss_usd_per_1m == right.summary.input_cache_miss_usd_per_1m &&
         left.summary.output_usd_per_1m == right.summary.output_usd_per_1m &&
         left.summary.metadata_source_uri == right.summary.metadata_source_uri &&
         left.summary.metadata_effective_at == right.summary.metadata_effective_at &&
         left.summary.verification_state == right.summary.verification_state &&
         left.display_name == right.display_name &&
         left.reasoning_mode == right.reasoning_mode && left.pricing_ref == right.pricing_ref &&
         left.aliases == right.aliases && left.input_modalities == right.input_modalities &&
         left.feature_notes == right.feature_notes &&
         left.response_private_fields == right.response_private_fields &&
         left.verification_states == right.verification_states &&
         left.supports_streaming == right.supports_streaming &&
         left.supports_json_object == right.supports_json_object &&
         left.supports_json_schema == right.supports_json_schema &&
         left.supports_native_stream_usage == right.supports_native_stream_usage;
}

bool apply_provider_overlay(ProviderCatalogProvider& current,
                            const ProviderCatalogProvider& overlay,
                            std::string& error) {
  if (!same_provider_static_fields(current, overlay)) {
    error = "provider overlay attempted to change immutable provider fields for " +
            overlay.descriptor.provider_id;
    return false;
  }

  current.runtime.mutable_overlay_fields =
      merge_unique_vectors(current.runtime.mutable_overlay_fields, overlay.runtime.mutable_overlay_fields);
  current.descriptor.source_version = overlay.descriptor.source_version;
  current.runtime.source_layer = overlay.runtime.source_layer;

  if (overlay.descriptor.auth_ref != current.descriptor.auth_ref) {
    if (!overlay.runtime.overlay_field_is_mutable("auth_ref")) {
      error = "provider overlay attempted to change immutable field auth_ref for " +
              overlay.descriptor.provider_id;
      return false;
    }
    current.descriptor.auth_ref = overlay.descriptor.auth_ref;
  }

  if (overlay.descriptor.header_refs != current.descriptor.header_refs) {
    if (!overlay.runtime.overlay_field_is_mutable("header_refs")) {
      error = "provider overlay attempted to change immutable field header_refs for " +
              overlay.descriptor.provider_id;
      return false;
    }
    current.descriptor.header_refs = overlay.descriptor.header_refs;
  }

  if (overlay.runtime.base_url_alias != current.runtime.base_url_alias) {
    if (!overlay.runtime.overlay_field_is_mutable("base_url_alias")) {
      error = "provider overlay attempted to change immutable field base_url_alias for " +
              overlay.descriptor.provider_id;
      return false;
    }
    current.runtime.base_url_alias = overlay.runtime.base_url_alias;
  }

  if (overlay.runtime.activation_flag != current.runtime.activation_flag) {
    if (!overlay.runtime.overlay_field_is_mutable("activation_flag")) {
      error = "provider overlay attempted to change immutable field activation_flag for " +
              overlay.descriptor.provider_id;
      return false;
    }
    current.runtime.activation_flag = overlay.runtime.activation_flag;
  }

  return true;
}

bool merge_provider_package(ProviderCatalogSnapshot& snapshot,
                            const ProviderPackage& package,
                            std::string& error) {
  auto provider_it = std::find_if(snapshot.providers.begin(), snapshot.providers.end(),
                                  [&](const ProviderCatalogProvider& candidate) {
                                    return candidate.descriptor.provider_id ==
                                           package.provider.descriptor.provider_id;
                                  });
  if (provider_it == snapshot.providers.end()) {
    snapshot.providers.push_back(package.provider);
  } else if (!apply_provider_overlay(*provider_it, package.provider, error)) {
    return false;
  }

  for (const auto& model : package.models) {
    auto model_it = std::find_if(snapshot.models.begin(), snapshot.models.end(),
                                 [&](const ProviderModelMetadata& candidate) {
                                   return candidate.summary.provider_id == model.summary.provider_id &&
                                          candidate.summary.model_id == model.summary.model_id;
                                 });
    if (model_it == snapshot.models.end()) {
      snapshot.models.push_back(model);
      continue;
    }

    if (!same_model_static_fields(*model_it, model)) {
      error = "provider overlay attempted to change immutable model metadata for " +
              model.summary.provider_id + "/" + model.summary.model_id;
      return false;
    }
  }

  return true;
}

LayerCatalogBuildResult load_layer_catalog(const std::filesystem::path& root,
                                           const std::string& source_layer,
                                           bool require_existing_root) {
  LayerCatalogBuildResult result;
  const CatalogIndexResult index = load_catalog_index(root, require_existing_root);
  if (!index.ok) {
    result.error = index.error;
    return result;
  }

  result.layer.default_source_version = index.index.default_source_version;
  for (const auto& package_name : index.index.packages) {
    const auto package = load_provider_package(root / package_name, source_layer,
                                               index.index.default_source_version);
    if (!package.ok) {
      result.error = package.error;
      return result;
    }

    result.layer.packages.push_back(package.package);
  }

  result.ok = true;
  return result;
}

ProviderCatalogSnapshot build_provider_snapshot(const CatalogConfig& config, std::string& error) {
  const auto baseline = load_layer_catalog(config.baseline_root, "baseline", true);
  if (!baseline.ok) {
    error = baseline.error;
    return {};
  }

  ProviderCatalogSnapshot snapshot;
  snapshot.default_source_version = baseline.layer.default_source_version;
  for (const auto& package : baseline.layer.packages) {
    if (!merge_provider_package(snapshot, package, error)) {
      return {};
    }
  }

  const auto deployment = load_layer_catalog(config.deployment_root, "deployment", false);
  if (!deployment.ok) {
    error = deployment.error;
    return {};
  }

  if (!deployment.layer.default_source_version.empty()) {
    snapshot.default_source_version = deployment.layer.default_source_version;
  }
  for (const auto& package : deployment.layer.packages) {
    if (!merge_provider_package(snapshot, package, error)) {
      return {};
    }
  }

  const auto runtime = load_layer_catalog(config.snapshot_cache_root, "snapshot", false);
  if (!runtime.ok) {
    error = runtime.error;
    return {};
  }

  if (!runtime.layer.default_source_version.empty()) {
    snapshot.default_source_version = runtime.layer.default_source_version;
  }
  for (const auto& package : runtime.layer.packages) {
    if (!merge_provider_package(snapshot, package, error)) {
      return {};
    }
  }

  if (snapshot.default_source_version.empty()) {
    snapshot.default_source_version = "1";
  }

  return snapshot;
}

}  // namespace

namespace dasall::llm::provider {

bool ProviderRuntimeSettings::has_consistent_values() const {
  return !display_name.empty() && !auth_mode.empty() && !trusted_source.empty() &&
         !source_layer.empty() && !base_url_alias.empty();
}

bool ProviderRuntimeSettings::overlay_field_is_mutable(std::string_view field) const {
  return std::find(mutable_overlay_fields.begin(), mutable_overlay_fields.end(), field) !=
         mutable_overlay_fields.end();
}

bool ProviderCatalogProvider::has_consistent_values() const {
  return !descriptor.provider_id.empty() && !descriptor.adapter_family.empty() &&
         !descriptor.api_family.empty() && !descriptor.base_url.empty() &&
         !descriptor.auth_ref.empty() && !descriptor.source_version.empty() &&
         runtime.has_consistent_values();
}

bool ProviderModelMetadata::has_consistent_values() const {
  return !summary.provider_id.empty() && !summary.model_id.empty() &&
         !summary.model_version.empty() && summary.context_window > 0U &&
         summary.default_max_output_tokens > 0U && summary.max_output_tokens_hard_limit > 0U &&
         !summary.metadata_source_uri.empty() && !summary.metadata_effective_at.empty() &&
         !summary.verification_state.empty() && !display_name.empty() && !pricing_ref.empty() &&
         !aliases.empty() && !input_modalities.empty() && !verification_states.empty();
}

std::string ProviderModelMetadata::verification_state_for(std::string_view capability) const {
  const auto it = verification_states.find(std::string(capability));
  if (it == verification_states.end()) {
    return {};
  }

  return it->second;
}

bool ProviderCatalogSnapshot::has_consistent_values() const {
  return !providers.empty() && !models.empty() &&
         std::all_of(providers.begin(), providers.end(), [](const ProviderCatalogProvider& provider) {
           return provider.has_consistent_values();
         }) &&
         std::all_of(models.begin(), models.end(), [](const ProviderModelMetadata& model) {
           return model.has_consistent_values();
         });
}

const ProviderCatalogProvider* ProviderCatalogSnapshot::find_provider(
    std::string_view provider_id) const {
  const auto it = std::find_if(providers.begin(), providers.end(),
                               [&](const ProviderCatalogProvider& provider) {
                                 return provider.descriptor.provider_id == provider_id;
                               });
  if (it == providers.end()) {
    return nullptr;
  }

  return &(*it);
}

const ProviderModelMetadata* ProviderCatalogSnapshot::find_model(std::string_view provider_id,
                                                                 std::string_view model_id) const {
  const auto it = std::find_if(models.begin(), models.end(), [&](const ProviderModelMetadata& model) {
    return model.summary.provider_id == provider_id && model.summary.model_id == model_id;
  });
  if (it == models.end()) {
    return nullptr;
  }

  return &(*it);
}

bool ProviderCatalogRepository::init(const ProviderCatalogSourceConfig& config) {
  if (!config.has_consistent_values()) {
    last_error_message_ = "provider catalog repository received inconsistent source config";
    return false;
  }

  config_ = config;
  initialized_ = true;
  return reload();
}

bool ProviderCatalogRepository::reload() {
  if (!initialized_) {
    last_error_message_ = "provider catalog repository has not been initialized";
    return false;
  }

  std::string error;
  ProviderCatalogSnapshot snapshot = build_provider_snapshot(config_, error);
  if (!error.empty() || !snapshot.has_consistent_values()) {
    last_error_message_ = error.empty() ? "provider catalog failed consistency checks" : error;
    return false;
  }

  auto immutable_snapshot = std::make_shared<const ProviderCatalogSnapshot>(std::move(snapshot));
  std::atomic_store_explicit(&catalog_snapshot_, immutable_snapshot, std::memory_order_release);
  last_error_message_.clear();
  return true;
}

std::shared_ptr<const ProviderCatalogSnapshot> ProviderCatalogRepository::snapshot() const {
  return std::atomic_load_explicit(&catalog_snapshot_, std::memory_order_acquire);
}

std::string ProviderCatalogRepository::last_error_message() const {
  return last_error_message_;
}

}  // namespace dasall::llm::provider