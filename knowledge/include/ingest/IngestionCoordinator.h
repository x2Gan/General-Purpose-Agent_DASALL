#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "index/CorpusCatalog.h"
#include "ingest/Chunker.h"

namespace dasall::knowledge::ingest {

struct IndexUpdateBatch {
  std::string batch_id;
  std::vector<ChunkRecord> chunk_records;
  std::vector<std::string> removed_document_ids;
  std::vector<std::string> warnings;
  std::optional<bool> vector_enabled;

  [[nodiscard]] bool has_consistent_values() const;
};

struct IngestionCoordinatorDeps {
  std::function<index::CorpusCatalogSnapshot()> load_catalog_snapshot;
  std::function<std::vector<SourceRecord>(std::string_view corpus_id)> load_inventory;
  std::function<std::filesystem::path()> repository_root;
  std::function<std::int64_t()> now_ms;
};

class IngestionCoordinator {
 public:
  explicit IngestionCoordinator(IngestionCoordinatorDeps deps = {},
                                ChunkPolicy chunk_policy = {});

  [[nodiscard]] IndexUpdateBatch build_update_batch(const CorpusChangeSet& change_set) const;

 private:
  struct ScanAndCanonicalizeResult {
    std::vector<CanonicalDocument> documents;
    std::vector<std::string> removed_document_ids;
    std::vector<std::string> warnings;
  };

  [[nodiscard]] ScanAndCanonicalizeResult scan_and_canonicalize(
      const CorpusChangeSet& change_set) const;
  [[nodiscard]] std::vector<ChunkRecord> build_chunk_records(
      const std::vector<CanonicalDocument>& documents) const;
  [[nodiscard]] std::vector<CorpusDescriptor> select_target_corpora(
      const CorpusChangeSet& change_set,
      const index::CorpusCatalogSnapshot& snapshot) const;
  [[nodiscard]] std::string build_batch_id(const IndexUpdateBatch& batch) const;
  [[nodiscard]] std::string build_document_lineage_id(std::string_view source_id) const;

  IngestionCoordinatorDeps deps_;
  ChunkPolicy chunk_policy_;
};

}  // namespace dasall::knowledge::ingest