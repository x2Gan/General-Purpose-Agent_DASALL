#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "KnowledgeTypes.h"

namespace dasall::knowledge::index {

struct CorpusCatalogDelta {
  std::vector<CorpusDescriptor> upserted_descriptors;
  std::vector<std::string> removed_corpus_ids;

  [[nodiscard]] bool has_consistent_values() const;
};

struct CorpusCatalogDeps {
  std::filesystem::path catalog_path;
};

class CorpusCatalogSnapshot {
 public:
  CorpusCatalogSnapshot();

  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] std::vector<CorpusDescriptor> list_all() const;
  [[nodiscard]] std::optional<CorpusDescriptor> find_by_id(std::string_view corpus_id) const;
  [[nodiscard]] std::vector<CorpusDescriptor> filter_by_tags(
      const std::vector<std::string>& tags) const;
  [[nodiscard]] std::vector<CorpusDescriptor> filter_by_mode(RetrievalMode mode) const;

 private:
  struct State;

  explicit CorpusCatalogSnapshot(std::shared_ptr<const State> state);

  std::shared_ptr<const State> state_;

  friend class CorpusCatalog;
};

class CorpusCatalog {
 public:
  CorpusCatalog();
  explicit CorpusCatalog(CorpusCatalogDeps deps);
  explicit CorpusCatalog(CorpusCatalogSnapshot initial_snapshot);

  [[nodiscard]] CorpusCatalogSnapshot snapshot() const;
  [[nodiscard]] bool replace_all(std::vector<CorpusDescriptor> descriptors);
  [[nodiscard]] bool apply_delta(const CorpusCatalogDelta& delta);

 private:
  [[nodiscard]] bool persist_snapshot(const CorpusCatalogSnapshot& snapshot) const;

  CorpusCatalogSnapshot active_snapshot_;
  CorpusCatalogDeps deps_{};
};

}  // namespace dasall::knowledge::index