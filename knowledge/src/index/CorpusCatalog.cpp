#include "index/CorpusCatalog.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace dasall::knowledge::index {

namespace {

[[nodiscard]] bool descriptor_list_is_consistent(
    const std::vector<CorpusDescriptor>& descriptors) {
  std::set<std::string> corpus_ids;
  std::set<std::string> source_uris;

  for (const auto& descriptor : descriptors) {
    if (!descriptor.has_consistent_values()) {
      return false;
    }

    if (!corpus_ids.insert(descriptor.corpus_id).second) {
      return false;
    }

    if (!source_uris.insert(descriptor.source_uri).second) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool descriptor_matches_all_tags(const CorpusDescriptor& descriptor,
                                               const std::vector<std::string>& tags) {
  return std::all_of(tags.begin(), tags.end(), [&descriptor](const std::string& tag) {
    return std::find(descriptor.tags.begin(), descriptor.tags.end(), tag) != descriptor.tags.end();
  });
}

[[nodiscard]] bool descriptor_supports_mode(const CorpusDescriptor& descriptor,
                                            RetrievalMode mode) {
  return std::find(descriptor.supported_modes.begin(), descriptor.supported_modes.end(), mode) !=
         descriptor.supported_modes.end();
}

}  // namespace

struct CorpusCatalogSnapshot::State {
  std::vector<CorpusDescriptor> descriptors;
  std::map<std::string, std::size_t, std::less<>> descriptors_by_id;
  std::map<std::string, std::size_t, std::less<>> descriptors_by_source_uri;
};

bool CorpusCatalogDelta::has_consistent_values() const {
  std::set<std::string> upserted_ids;
  std::set<std::string> removed_ids;

  for (const auto& descriptor : upserted_descriptors) {
    if (!descriptor.has_consistent_values()) {
      return false;
    }

    if (!upserted_ids.insert(descriptor.corpus_id).second) {
      return false;
    }
  }

  for (const auto& removed_corpus_id : removed_corpus_ids) {
    if (removed_corpus_id.empty()) {
      return false;
    }

    if (!removed_ids.insert(removed_corpus_id).second) {
      return false;
    }

    if (upserted_ids.contains(removed_corpus_id)) {
      return false;
    }
  }

  return true;
}

CorpusCatalogSnapshot::CorpusCatalogSnapshot()
    : state_(std::make_shared<const State>()) {}

CorpusCatalogSnapshot::CorpusCatalogSnapshot(std::shared_ptr<const State> state)
    : state_(state ? std::move(state) : std::make_shared<const State>()) {}

bool CorpusCatalogSnapshot::empty() const {
  return size() == 0U;
}

std::size_t CorpusCatalogSnapshot::size() const {
  return state_->descriptors.size();
}

bool CorpusCatalogSnapshot::has_consistent_values() const {
  return state_ != nullptr && descriptor_list_is_consistent(state_->descriptors) &&
         state_->descriptors.size() == state_->descriptors_by_id.size() &&
         state_->descriptors.size() == state_->descriptors_by_source_uri.size();
}

std::vector<CorpusDescriptor> CorpusCatalogSnapshot::list_all() const {
  return state_->descriptors;
}

std::optional<CorpusDescriptor> CorpusCatalogSnapshot::find_by_id(std::string_view corpus_id) const {
  const auto descriptor_it = state_->descriptors_by_id.find(corpus_id);
  if (descriptor_it == state_->descriptors_by_id.end()) {
    return std::nullopt;
  }

  return state_->descriptors.at(descriptor_it->second);
}

std::vector<CorpusDescriptor> CorpusCatalogSnapshot::filter_by_tags(
    const std::vector<std::string>& tags) const {
  if (tags.empty()) {
    return list_all();
  }

  std::vector<CorpusDescriptor> matches;
  matches.reserve(state_->descriptors.size());
  for (const auto& descriptor : state_->descriptors) {
    if (descriptor_matches_all_tags(descriptor, tags)) {
      matches.push_back(descriptor);
    }
  }

  return matches;
}

std::vector<CorpusDescriptor> CorpusCatalogSnapshot::filter_by_mode(RetrievalMode mode) const {
  std::vector<CorpusDescriptor> matches;
  matches.reserve(state_->descriptors.size());
  for (const auto& descriptor : state_->descriptors) {
    if (descriptor_supports_mode(descriptor, mode)) {
      matches.push_back(descriptor);
    }
  }

  return matches;
}

CorpusCatalog::CorpusCatalog() = default;

CorpusCatalog::CorpusCatalog(CorpusCatalogSnapshot initial_snapshot)
    : active_snapshot_(std::move(initial_snapshot)) {}

CorpusCatalogSnapshot CorpusCatalog::snapshot() const {
  return active_snapshot_;
}

bool CorpusCatalog::replace_all(std::vector<CorpusDescriptor> descriptors) {
  if (!descriptor_list_is_consistent(descriptors)) {
    return false;
  }

  auto state = std::make_shared<CorpusCatalogSnapshot::State>();
  state->descriptors = std::move(descriptors);
  for (std::size_t descriptor_index = 0; descriptor_index < state->descriptors.size();
       ++descriptor_index) {
    const auto& descriptor = state->descriptors[descriptor_index];
    state->descriptors_by_id.emplace(descriptor.corpus_id, descriptor_index);
    state->descriptors_by_source_uri.emplace(descriptor.source_uri, descriptor_index);
  }

  active_snapshot_ = CorpusCatalogSnapshot(std::shared_ptr<const CorpusCatalogSnapshot::State>(
      std::move(state)));
  return true;
}

bool CorpusCatalog::apply_delta(const CorpusCatalogDelta& delta) {
  if (!delta.has_consistent_values()) {
    return false;
  }

  auto next_descriptors = active_snapshot_.list_all();
  for (const auto& removed_corpus_id : delta.removed_corpus_ids) {
    std::erase_if(next_descriptors, [&removed_corpus_id](const CorpusDescriptor& descriptor) {
      return descriptor.corpus_id == removed_corpus_id;
    });
  }

  for (const auto& upserted_descriptor : delta.upserted_descriptors) {
    const auto existing_descriptor_it = std::find_if(
        next_descriptors.begin(), next_descriptors.end(), [&upserted_descriptor](const CorpusDescriptor& descriptor) {
          return descriptor.corpus_id == upserted_descriptor.corpus_id;
        });

    if (existing_descriptor_it == next_descriptors.end()) {
      next_descriptors.push_back(upserted_descriptor);
      continue;
    }

    *existing_descriptor_it = upserted_descriptor;
  }

  return replace_all(std::move(next_descriptors));
}

}  // namespace dasall::knowledge::index