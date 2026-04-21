#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "KnowledgeErrors.h"
#include "health/FreshnessController.h"
#include "retrieve/SparseRetriever.h"

namespace dasall::knowledge::index {

struct IndexSnapshot {
  IndexManifest manifest;
  std::string checksum;
  std::function<retrieve::SparseIndexSearchResult(
      const retrieve::SparseIndexSearchRequest& request)>
      search;

  [[nodiscard]] bool has_consistent_values() const;
};

class IndexReader {
 public:
  IndexReader();
  explicit IndexReader(std::shared_ptr<const IndexSnapshot> initial_snapshot);

  [[nodiscard]] retrieve::SparseIndexSearchResult search_sparse(
      const retrieve::SparseIndexSearchRequest& request) const;
  [[nodiscard]] std::optional<IndexManifest> current_manifest() const;
  [[nodiscard]] std::optional<std::string> read_snapshot_checksum(
      std::string_view snapshot_id) const;

  [[nodiscard]] bool swap_active_snapshot(std::shared_ptr<const IndexSnapshot> snapshot);

 private:
  [[nodiscard]] std::shared_ptr<const IndexSnapshot> load_active_snapshot() const;

  std::shared_ptr<const IndexSnapshot> active_snapshot_;
};

}  // namespace dasall::knowledge::index