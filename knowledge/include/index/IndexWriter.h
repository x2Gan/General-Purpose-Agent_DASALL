#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "KnowledgeErrors.h"
#include "index/IndexReader.h"
#include "index/VersionLedger.h"
#include "ingest/IngestionCoordinator.h"

namespace dasall::knowledge::index {

struct UpdateReport {
  bool ok = false;
  std::string snapshot_id;
  std::optional<IndexManifest> manifest;
  std::vector<std::string> warnings;
  std::optional<dasall::contracts::ErrorInfo> error;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RebuildPlan {
  std::string rebuild_reason;
  std::vector<ingest::ChunkRecord> chunk_records;
  std::vector<std::string> warnings;
  std::string tokenizer_profile = "porter unicode61 remove_diacritics 1";
  bool vector_enabled = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct RebuildReport {
  bool ok = false;
  std::string snapshot_id;
  std::optional<IndexManifest> manifest;
  std::vector<std::string> warnings;
  std::optional<dasall::contracts::ErrorInfo> error;

  [[nodiscard]] bool has_consistent_values() const;
};

struct IndexWriterDeps {
  std::function<std::filesystem::path()> snapshots_root;
  std::function<std::int64_t()> now_ms;
  std::function<bool(const VersionLedgerEntry& entry)> record_candidate;
  std::function<bool(std::string_view snapshot_id, std::int64_t activated_at)> mark_active;
  std::function<bool(const IndexManifest& manifest)> refresh_catalog;
};

class IndexWriter {
 public:
  explicit IndexWriter(IndexReader& reader,
                       VersionLedger& ledger,
                       IndexWriterDeps deps = {});

  [[nodiscard]] UpdateReport apply_update_batch(const ingest::IndexUpdateBatch& batch);
  [[nodiscard]] RebuildReport rebuild_all(const RebuildPlan& plan);

 private:
  struct ShadowIndex {
    std::filesystem::path snapshot_dir;
    std::filesystem::path database_path;
    std::filesystem::path manifest_path;
    IndexManifest manifest;
    std::string checksum;
    std::shared_ptr<const IndexSnapshot> snapshot;

    [[nodiscard]] bool has_consistent_values() const;
  };

  [[nodiscard]] ShadowIndex build_shadow_index(const ingest::IndexUpdateBatch& batch) const;
  [[nodiscard]] ShadowIndex build_shadow_index(const ingest::IndexUpdateBatch& batch,
                                               std::string_view tokenizer_profile,
                                               bool vector_enabled,
                                               bool seed_from_active) const;
  [[nodiscard]] bool swap_active_snapshot(const ShadowIndex& shadow);

  IndexReader& reader_;
  VersionLedger& ledger_;
  IndexWriterDeps deps_;
  std::optional<ShadowIndex> active_shadow_;
  std::optional<ShadowIndex> last_known_good_shadow_;
};

}  // namespace dasall::knowledge::index