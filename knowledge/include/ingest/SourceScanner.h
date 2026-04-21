#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "KnowledgeTypes.h"

namespace dasall::knowledge::ingest {

struct CorpusScanPlan {
  std::string corpus_id;
  std::string root_uri;
  SourceKind source_kind = SourceKind::File;
  std::vector<std::string> include_globs;
  std::vector<std::string> exclude_globs;
  std::vector<SourceFormat> allowed_formats{SourceFormat::Markdown};
  bool full_scan = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SourceRecord {
  std::string source_id;
  std::string corpus_id;
  std::string source_uri;
  std::string content_hash;
  std::string version;
  std::int64_t updated_at_ms = 0;
  SourceKind kind = SourceKind::File;
  SourceFormat format = SourceFormat::Markdown;
  AuthorityLevel authority_level = AuthorityLevel::Reference;
  std::string language = "und";
  std::vector<std::string> tags;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SourceScanDelta {
  std::vector<SourceRecord> added;
  std::vector<SourceRecord> updated;
  std::vector<std::string> removed_source_ids;
  std::vector<std::string> quarantined_source_ids;
  bool full_scan = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SourceScannerDeps {
  std::function<std::optional<CorpusDescriptor>(std::string_view corpus_id)> lookup_corpus;
  std::function<std::vector<SourceRecord>(std::string_view corpus_id)> load_inventory;
  std::function<std::filesystem::path()> repository_root;
  std::function<std::int64_t()> now_ms;
};

class SourceScanner {
 public:
  explicit SourceScanner(SourceScannerDeps deps = {});

  [[nodiscard]] SourceScanDelta scan(const CorpusScanPlan& plan) const;

 private:
  [[nodiscard]] std::string compute_source_hash(std::string_view content) const;
  [[nodiscard]] std::optional<SourceFormat> detect_format(const std::filesystem::path& path) const;
  [[nodiscard]] std::optional<SourceRecord> build_source_record(
      const std::filesystem::path& path,
      const CorpusDescriptor& descriptor) const;
  [[nodiscard]] std::filesystem::path resolve_root(const std::string& root_uri) const;
  [[nodiscard]] std::int64_t resolve_updated_at_ms(const std::filesystem::path& path) const;
  [[nodiscard]] std::string make_relative_source_uri(const std::filesystem::path& path) const;
  [[nodiscard]] std::string make_source_id(std::string_view corpus_id,
                                           std::string_view source_uri) const;
  [[nodiscard]] bool is_glob_match(std::string_view candidate, std::string_view pattern) const;

  SourceScannerDeps deps_;
};

}  // namespace dasall::knowledge::ingest