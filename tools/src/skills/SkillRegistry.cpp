#include "skills/SkillRegistry.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <sstream>
#include <tuple>

namespace dasall::tools::skills {

namespace {

[[nodiscard]] bool has_unique_non_empty_values(const std::vector<std::string>& values) {
  std::vector<std::string> sorted_values;
  sorted_values.reserve(values.size());

  for (const auto& value : values) {
    if (value.empty()) {
      return false;
    }
    sorted_values.push_back(value);
  }

  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

[[nodiscard]] std::string normalize_phrase(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());

  bool last_was_space = true;
  for (const char character : value) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    if (std::isalnum(unsigned_character) != 0) {
      normalized.push_back(
          static_cast<char>(std::tolower(unsigned_character)));
      last_was_space = false;
      continue;
    }

    if (!last_was_space) {
      normalized.push_back(' ');
      last_was_space = true;
    }
  }

  if (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }

  return normalized;
}

[[nodiscard]] std::vector<std::string> tokenize(std::string_view value) {
  std::vector<std::string> tokens;
  std::istringstream stream(normalize_phrase(value));
  std::string token;
  while (stream >> token) {
    if (std::find(tokens.begin(), tokens.end(), token) == tokens.end()) {
      tokens.push_back(token);
    }
  }

  return tokens;
}

[[nodiscard]] std::vector<std::string> normalize_labels(
    const std::vector<std::string>& labels) {
  std::vector<std::string> normalized_labels;
  normalized_labels.reserve(labels.size());
  for (const auto& label : labels) {
    const auto normalized = normalize_phrase(label);
    if (!normalized.empty() &&
        std::find(normalized_labels.begin(), normalized_labels.end(), normalized) ==
            normalized_labels.end()) {
      normalized_labels.push_back(normalized);
    }
  }

  return normalized_labels;
}

void append_unique_terms(
    std::vector<std::string>& destination,
    const std::vector<std::string>& source) {
  for (const auto& value : source) {
    if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
      destination.push_back(value);
    }
  }
}

[[nodiscard]] std::size_t count_overlap(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right,
    std::vector<std::string>& matched_terms) {
  std::size_t overlap = 0U;
  for (const auto& value : left) {
    if (std::find(right.begin(), right.end(), value) != right.end()) {
      ++overlap;
      matched_terms.push_back(value);
    }
  }

  return overlap;
}

[[nodiscard]] bool profile_matches(
    const SkillSpecAsset& asset,
    const std::optional<std::string>& profile_id) {
  if (asset.profile_constraints.empty()) {
    return true;
  }

  if (!profile_id.has_value() || profile_id->empty()) {
    return false;
  }

  return std::find(
             asset.profile_constraints.begin(),
             asset.profile_constraints.end(),
             *profile_id) != asset.profile_constraints.end();
}

[[nodiscard]] bool should_prefer_candidate(
    const SkillSpecAsset& candidate,
    const SkillSpecAsset& current) {
  return std::tie(candidate.source_key, candidate.skill_id) <
         std::tie(current.source_key, current.skill_id);
}

[[nodiscard]] std::size_t compute_match_score(
    const SkillSpecAsset& asset,
    const SkillMatchQuery& query,
    std::vector<std::string>& matched_terms) {
  const auto query_tokens = tokenize(query.intent_text);
  if (query_tokens.empty()) {
    return 0U;
  }

  const auto normalized_query = normalize_phrase(query.intent_text);
  std::size_t best_pattern_score = 0U;
  std::vector<std::string> best_pattern_terms;

  for (const auto& intent_pattern : asset.intent_patterns) {
    const auto pattern_tokens = tokenize(intent_pattern);
    if (pattern_tokens.empty()) {
      continue;
    }

    std::vector<std::string> overlap_terms;
    const auto overlap = count_overlap(query_tokens, pattern_tokens, overlap_terms);
    const auto normalized_pattern = normalize_phrase(intent_pattern);
    const bool exact_phrase_match =
        !normalized_pattern.empty() &&
        normalized_query.find(normalized_pattern) != std::string::npos;
    if (overlap == 0U && !exact_phrase_match) {
      continue;
    }

    std::size_t score = overlap * 10U + pattern_tokens.size();
    if (exact_phrase_match) {
      score += 5U;
      append_unique_terms(overlap_terms, pattern_tokens);
    }

    if (score > best_pattern_score) {
      best_pattern_score = score;
      best_pattern_terms = std::move(overlap_terms);
    }
  }

  if (best_pattern_score == 0U) {
    return 0U;
  }

  const auto query_tags = normalize_labels(query.tags);
  const auto asset_tags = normalize_labels(asset.tags);
  std::vector<std::string> tag_terms;
  const auto tag_overlap = count_overlap(query_tags, asset_tags, tag_terms);
  append_unique_terms(best_pattern_terms, tag_terms);
  matched_terms = std::move(best_pattern_terms);

  return best_pattern_score + tag_overlap * 2U;
}

}  // namespace

bool SkillSpecAsset::has_consistent_values() const {
  if (skill_id.empty() || source_key.empty() || asset_ref.empty() || version.empty() ||
      name.empty() || description.empty() || workflow_template_ref.empty() ||
      eval_suite_ref.empty() || fallback_mode.empty() || intent_patterns.empty() ||
      allowed_tools.empty()) {
    return false;
  }

  if (prompt_bundle_ref.has_value() && prompt_bundle_ref->empty()) {
    return false;
  }

  return has_unique_non_empty_values(intent_patterns) &&
         has_unique_non_empty_values(allowed_tools) &&
         has_unique_non_empty_values(tags) &&
         has_unique_non_empty_values(profile_constraints) &&
         has_unique_non_empty_values(required_domains);
}

std::shared_ptr<const SkillRegistrySnapshot> SkillRegistry::snapshot() const {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

bool SkillRegistry::register_asset(const SkillSpecAsset& asset) {
  if (!asset.has_consistent_values()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  if (!upsert_asset(next_snapshot, asset)) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

SkillMatchResult SkillRegistry::match_intent(const SkillMatchQuery& query) const {
  if (query.intent_text.empty()) {
    return SkillMatchResult{
        .matched = false,
        .asset = std::nullopt,
        .reason_code = "skill.match.missing_intent",
        .matched_terms = {},
        .score = 0U,
    };
  }

  const auto current_snapshot = snapshot();
  if (current_snapshot->assets.empty()) {
    return SkillMatchResult{
        .matched = false,
        .asset = std::nullopt,
        .reason_code = "skill.match.empty_registry",
        .matched_terms = {},
        .score = 0U,
    };
  }

  SkillMatchResult best_match{
      .matched = false,
      .asset = std::nullopt,
      .reason_code = "skill.match.none",
      .matched_terms = {},
      .score = 0U,
  };
  bool saw_profile_filtered = false;

  for (const auto& asset : current_snapshot->assets) {
    if (!profile_matches(asset, query.profile_id)) {
      saw_profile_filtered = true;
      continue;
    }

    std::vector<std::string> matched_terms;
    const auto score = compute_match_score(asset, query, matched_terms);
    if (score == 0U) {
      continue;
    }

    if (!best_match.matched || score > best_match.score ||
        (score == best_match.score &&
         should_prefer_candidate(asset, *best_match.asset))) {
      best_match.matched = true;
      best_match.asset = asset;
      best_match.reason_code = "skill.match.selected";
      best_match.matched_terms = std::move(matched_terms);
      best_match.score = score;
    }
  }

  if (best_match.matched) {
    return best_match;
  }

  return SkillMatchResult{
      .matched = false,
      .asset = std::nullopt,
      .reason_code = saw_profile_filtered ? "skill.match.profile_filtered"
                                          : "skill.match.none",
      .matched_terms = {},
      .score = 0U,
  };
}

bool SkillRegistry::revoke_source(std::string_view source_key) {
  if (source_key.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  if (next_snapshot.assets_by_source.erase(std::string(source_key)) == 0U) {
    return false;
  }

  rebuild_flat_view(next_snapshot);
  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

std::vector<SkillSpecAsset> SkillRegistry::list_assets() const {
  return snapshot()->assets;
}

bool SkillRegistry::upsert_asset(
    SkillRegistrySnapshot& snapshot,
    const SkillSpecAsset& asset) {
  auto& source_assets = snapshot.assets_by_source[asset.source_key];
  auto existing = std::find_if(
      source_assets.begin(),
      source_assets.end(),
      [&asset](const SkillSpecAsset& candidate) {
        return candidate.skill_id == asset.skill_id;
      });

  if (existing != source_assets.end()) {
    *existing = asset;
  } else {
    source_assets.push_back(asset);
  }

  std::sort(
      source_assets.begin(),
      source_assets.end(),
      [](const SkillSpecAsset& left, const SkillSpecAsset& right) {
        return left.skill_id < right.skill_id;
      });
  rebuild_flat_view(snapshot);
  return true;
}

void SkillRegistry::rebuild_flat_view(SkillRegistrySnapshot& snapshot) {
  snapshot.assets.clear();
  for (const auto& [source_key, source_assets] : snapshot.assets_by_source) {
    static_cast<void>(source_key);
    snapshot.assets.insert(
        snapshot.assets.end(), source_assets.begin(), source_assets.end());
  }
}

void SkillRegistry::publish_snapshot(SkillRegistrySnapshot next_snapshot) {
  std::atomic_store_explicit(
      &snapshot_,
      std::make_shared<const SkillRegistrySnapshot>(std::move(next_snapshot)),
      std::memory_order_release);
}

}  // namespace dasall::tools::skills