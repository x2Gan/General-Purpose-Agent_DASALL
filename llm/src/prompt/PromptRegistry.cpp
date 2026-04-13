#include "PromptRegistry.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "LLMSubsystemConfig.h"

namespace {

using CompositionStage = dasall::contracts::CompositionStage;
using PromptAssetDescriptor = dasall::llm::prompt::PromptAssetDescriptor;
using PromptQuery = dasall::llm::prompt::PromptQuery;
using PromptRegistryConfig = dasall::llm::prompt::PromptRegistryConfig;
using PromptRegistryResult = dasall::llm::prompt::PromptRegistryResult;
using ResultCode = dasall::contracts::ResultCode;

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::optional<CompositionStage> parse_stage(std::string_view raw_stage) {
  const std::string stage = to_lower_copy(std::string(raw_stage));
  if (stage == "planning") {
    return CompositionStage::Planning;
  }

  if (stage == "execution") {
    return CompositionStage::Execution;
  }

  if (stage == "reflection") {
    return CompositionStage::Reflection;
  }

  if (stage == "response") {
    return CompositionStage::Response;
  }

  return std::nullopt;
}

std::vector<std::string> normalize_values(const std::vector<std::string>& values) {
  std::vector<std::string> normalized;
  normalized.reserve(values.size());

  for (const auto& value : values) {
    if (value.empty()) {
      continue;
    }

    if (std::find(normalized.begin(), normalized.end(), value) == normalized.end()) {
      normalized.push_back(value);
    }
  }

  return normalized;
}

bool contains_value(const std::vector<std::string>& values, std::string_view target) {
  return std::find(values.begin(), values.end(), target) != values.end();
}

std::vector<std::string> intersect_values(const std::vector<std::string>& left,
                                         const std::vector<std::string>& right) {
  std::vector<std::string> intersection;
  intersection.reserve(std::min(left.size(), right.size()));

  for (const auto& value : left) {
    if (!contains_value(right, value)) {
      continue;
    }

    if (!contains_value(intersection, value)) {
      intersection.push_back(value);
    }
  }

  return intersection;
}

std::vector<std::string> effective_trusted_sources(const PromptRegistryConfig& config,
                                                   const PromptQuery& query) {
  const std::vector<std::string> configured = normalize_values(config.trusted_sources);
  const std::vector<std::string> requested = normalize_values(query.trusted_sources);

  if (configured.empty()) {
    return requested;
  }

  if (requested.empty()) {
    return configured;
  }

  return intersect_values(configured, requested);
}

bool matches_stage(const PromptAssetDescriptor& descriptor, CompositionStage stage) {
  return descriptor.release.stage.has_value() && *descriptor.release.stage == stage;
}

bool matches_task_type(const PromptAssetDescriptor& descriptor, const PromptQuery& query) {
  if (query.task_type.empty()) {
    return true;
  }

  if (!descriptor.spec.task_types.has_value() || descriptor.spec.task_types->empty()) {
    return false;
  }

  return contains_value(*descriptor.spec.task_types, query.task_type);
}

bool matches_language(const PromptAssetDescriptor& descriptor, const PromptQuery& query) {
  if (query.language.empty()) {
    return true;
  }

  return descriptor.spec.language.has_value() && *descriptor.spec.language == query.language;
}

bool matches_model_family(const PromptAssetDescriptor& descriptor, const PromptQuery& query) {
  if (query.model_family.empty()) {
    return true;
  }

  return descriptor.spec.model_family.has_value() &&
         *descriptor.spec.model_family == query.model_family;
}

bool matches_base_dimensions(const PromptAssetDescriptor& descriptor,
                            const PromptQuery& query,
                            CompositionStage stage) {
  return matches_stage(descriptor, stage) && matches_task_type(descriptor, query) &&
         matches_language(descriptor, query) && matches_model_family(descriptor, query);
}

bool matches_scene_persona_selector(const PromptAssetDescriptor& descriptor,
                                    const PromptQuery& query) {
  if (query.scene_id.empty() && query.persona_id.empty()) {
    return false;
  }

  if (!query.scene_id.empty() && descriptor.scene_id != query.scene_id) {
    return false;
  }

  if (!query.persona_id.empty() && descriptor.persona_id != query.persona_id) {
    return false;
  }

  return true;
}

bool matches_profile_selector(const PromptAssetDescriptor& descriptor, const PromptQuery& query) {
  return !query.profile_id.empty() && contains_value(descriptor.profile_tags, query.profile_id);
}

std::optional<std::pair<std::string, std::string>> parse_prompt_release_id(
    std::string_view raw_release_id) {
  const std::size_t separator = raw_release_id.find('@');
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U >= raw_release_id.size()) {
    return std::nullopt;
  }

  return std::make_pair(std::string(raw_release_id.substr(0U, separator)),
                        std::string(raw_release_id.substr(separator + 1U)));
}

int eval_status_rank(const PromptAssetDescriptor& descriptor) {
  if (!descriptor.release.eval_status.has_value()) {
    return 0;
  }

  switch (*descriptor.release.eval_status) {
    case dasall::contracts::PromptEvalStatus::Stable:
      return 5;
    case dasall::contracts::PromptEvalStatus::Canary:
      return 4;
    case dasall::contracts::PromptEvalStatus::Experiment:
      return 3;
    case dasall::contracts::PromptEvalStatus::Draft:
      return 2;
    case dasall::contracts::PromptEvalStatus::Deprecated:
      return 1;
    case dasall::contracts::PromptEvalStatus::Unspecified:
      return 0;
  }

  return 0;
}

int source_layer_rank(const PromptAssetDescriptor& descriptor) {
  if (descriptor.source_layer == "snapshot") {
    return 3;
  }

  if (descriptor.source_layer == "deployment") {
    return 2;
  }

  if (descriptor.source_layer == "baseline") {
    return 1;
  }

  return 0;
}

const PromptAssetDescriptor* select_best_candidate(
    const std::vector<const PromptAssetDescriptor*>& candidates) {
  if (candidates.empty()) {
    return nullptr;
  }

  return *std::max_element(candidates.begin(), candidates.end(),
                           [](const PromptAssetDescriptor* left,
                              const PromptAssetDescriptor* right) {
                             return std::tuple(left->is_default_release,
                                               eval_status_rank(*left),
                                               source_layer_rank(*left),
                                               left->release.version.value_or(std::string()),
                                               std::string_view(left->package_id),
                                               left->release.prompt_id.value_or(std::string())) <
                                    std::tuple(right->is_default_release,
                                               eval_status_rank(*right),
                                               source_layer_rank(*right),
                                               right->release.version.value_or(std::string()),
                                               std::string_view(right->package_id),
                                               right->release.prompt_id.value_or(std::string()));
                           });
}

PromptRegistryResult make_failure(ResultCode code, std::string reason) {
  PromptRegistryResult result;
  result.code = code;
  result.selection_reason = std::move(reason);
  return result;
}

PromptRegistryResult make_success(const PromptAssetDescriptor& descriptor, std::string reason) {
  PromptRegistryResult result;
  result.release = descriptor.release;
  result.selected_prompt_id = descriptor.release.prompt_id.value_or(std::string());
  result.selected_version = descriptor.release.version.value_or(std::string());
  result.selection_reason = std::move(reason);
  if (descriptor.release.trusted_source.has_value()) {
    result.trusted_sources_matched.push_back(*descriptor.release.trusted_source);
  }
  return result;
}

}  // namespace

namespace dasall::llm::prompt {

bool PromptRegistry::init(const PromptRegistryConfig& config) {
  config_ = config;
  const auto previous_snapshot = repository_.snapshot();

  if (repository_.init(config.asset_sources)) {
    initialized_ = true;
    return true;
  }

  const auto retained_snapshot = repository_.snapshot();
  initialized_ = previous_snapshot != nullptr && retained_snapshot != nullptr;
  return false;
}

PromptRegistryResult PromptRegistry::select(const PromptQuery& query) const {
  if (!initialized_) {
    return make_failure(ResultCode::ValidationFieldMissing, "registry_not_initialized");
  }

  const auto stage = parse_stage(query.stage);
  if (!stage.has_value()) {
    return make_failure(ResultCode::ValidationFieldMissing, "invalid_stage");
  }

  const auto snapshot = repository_.snapshot();
  if (snapshot == nullptr || !snapshot->has_consistent_values()) {
    return make_failure(ResultCode::ValidationFieldMissing, "prompt_catalog_unavailable");
  }

  std::vector<const PromptAssetDescriptor*> base_candidates;
  for (const auto& descriptor : snapshot->descriptors) {
    if (matches_base_dimensions(descriptor, query, *stage)) {
      base_candidates.push_back(&descriptor);
    }
  }

  if (base_candidates.empty()) {
    return make_failure(ResultCode::ValidationFieldMissing, "no_matching_prompt_release");
  }

  const std::vector<std::string> configured_trusted_sources =
      normalize_values(config_.trusted_sources);
  const std::vector<std::string> requested_trusted_sources =
      normalize_values(query.trusted_sources);
  const std::vector<std::string> trusted_sources = effective_trusted_sources(config_, query);
  if (configured_trusted_sources.empty() && requested_trusted_sources.empty()) {
    return make_failure(ResultCode::PolicyDenied, "trusted_source_allowlist_missing");
  }

  if (trusted_sources.empty()) {
    return make_failure(ResultCode::PolicyDenied, "trusted_source_rejected");
  }

  auto trusted_match = [&](const PromptAssetDescriptor* descriptor) {
    return descriptor->release.trusted_source.has_value() &&
           contains_value(trusted_sources, *descriptor->release.trusted_source);
  };

  if (!query.prompt_release_id.empty()) {
    const auto release_id = parse_prompt_release_id(query.prompt_release_id);
    if (!release_id.has_value()) {
      return make_failure(ResultCode::ValidationFieldMissing, "invalid_prompt_release_id");
    }

    const auto explicit_candidate = std::find_if(
        base_candidates.begin(), base_candidates.end(), [&](const PromptAssetDescriptor* descriptor) {
          return descriptor->release.prompt_id == release_id->first &&
                 descriptor->release.version == release_id->second;
        });

    if (explicit_candidate == base_candidates.end()) {
      return make_failure(ResultCode::ValidationFieldMissing,
                          "explicit_prompt_release_not_found");
    }

    if (!trusted_match(*explicit_candidate)) {
      return make_failure(ResultCode::PolicyDenied, "trusted_source_rejected");
    }

    return make_success(**explicit_candidate, "explicit_prompt_release_id");
  }

  std::vector<const PromptAssetDescriptor*> trusted_candidates;
  trusted_candidates.reserve(base_candidates.size());
  std::copy_if(base_candidates.begin(), base_candidates.end(),
               std::back_inserter(trusted_candidates), trusted_match);

  if (trusted_candidates.empty()) {
    return make_failure(ResultCode::PolicyDenied, "trusted_source_rejected");
  }

  std::vector<const PromptAssetDescriptor*> scene_persona_candidates;
  std::vector<const PromptAssetDescriptor*> profile_candidates;
  std::vector<const PromptAssetDescriptor*> default_candidates;

  for (const auto* descriptor : trusted_candidates) {
    if (matches_scene_persona_selector(*descriptor, query)) {
      scene_persona_candidates.push_back(descriptor);
    }

    if (matches_profile_selector(*descriptor, query)) {
      profile_candidates.push_back(descriptor);
    }

    if (descriptor->is_default_release) {
      default_candidates.push_back(descriptor);
    }
  }

  if (const auto* selected = select_best_candidate(scene_persona_candidates);
      selected != nullptr) {
    return make_success(*selected, "scene_persona_selector");
  }

  if (const auto* selected = select_best_candidate(profile_candidates); selected != nullptr) {
    return make_success(*selected, "profile_selector");
  }

  if (const auto* selected = select_best_candidate(default_candidates); selected != nullptr) {
    return make_success(*selected, "default_release");
  }

  return make_failure(ResultCode::ValidationFieldMissing, "no_default_prompt_release");
}

}  // namespace dasall::llm::prompt