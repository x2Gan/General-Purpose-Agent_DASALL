#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ingest/SourceScanner.h"

namespace dasall::knowledge::ingest {

struct CanonicalDocument {
  std::string document_id;
  std::string corpus_id;
  std::string source_id;
  std::string source_uri;
  std::string title;
  std::string canonical_text;
  std::string source_hash;
  std::string version;
  std::int64_t updated_at_ms = 0;
  SourceFormat source_format = SourceFormat::Markdown;
  AuthorityLevel authority_level = AuthorityLevel::Reference;
  std::string language = "und";
  std::vector<std::string> tags;
  std::map<std::string, std::string> metadata;

  [[nodiscard]] bool has_consistent_values() const;
};

struct CanonicalizeResult {
  bool ok = false;
  std::optional<CanonicalDocument> document;
  std::vector<std::string> warnings;
  std::optional<std::string> quarantine_reason;

  [[nodiscard]] bool has_consistent_values() const;
};

struct CanonicalizerPolicy {
  std::filesystem::path repository_root;
};

class Canonicalizer {
 public:
  explicit Canonicalizer(CanonicalizerPolicy policy = {});

  [[nodiscard]] CanonicalizeResult canonicalize(const SourceRecord& source) const;

 private:
  [[nodiscard]] std::string normalize_markup(std::string_view raw_content) const;
  [[nodiscard]] std::map<std::string, std::string> extract_metadata(
      std::string_view normalized_content,
      const SourceRecord& source,
      const std::filesystem::path& source_path,
      std::vector<std::string>& warnings) const;
  [[nodiscard]] std::optional<std::string> canonicalize_yaml(std::string_view raw_content) const;
  [[nodiscard]] std::optional<std::string> read_source_text(const SourceRecord& source) const;
  [[nodiscard]] std::filesystem::path resolve_source_path(const SourceRecord& source) const;
  [[nodiscard]] std::string build_document_id(const SourceRecord& source,
                                              std::string_view version,
                                              std::string_view canonical_text) const;

  CanonicalizerPolicy policy_;
};

}  // namespace dasall::knowledge::ingest