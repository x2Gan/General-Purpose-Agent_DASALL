#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"

namespace dasall::knowledge::query {

struct NormalizedQuery {
  std::string request_id;
  std::string normalized_text;
  std::vector<std::string> lexical_terms;
  std::optional<RetrievalMode> preferred_mode;
  std::vector<std::string> domain_tags;
  std::vector<std::string> allowed_corpora;
  std::vector<std::string> required_tags;
  std::optional<std::string> required_language;
  KnowledgeQueryKind query_kind = KnowledgeQueryKind::FactLookup;
  std::size_t top_k = 8U;
  std::size_t max_context_projection_items = 6U;
  bool prefer_exact_match = false;
  bool allow_stale = false;
  std::vector<std::string> warnings;

  [[nodiscard]] bool has_consistent_values() const;
};

struct NormalizeResult {
  bool ok = false;
  std::optional<NormalizedQuery> normalized_query;
  std::optional<dasall::contracts::ErrorInfo> error;

  [[nodiscard]] bool has_consistent_values() const;
};

struct QueryNormalizePolicy {
  std::size_t max_query_text_bytes = 512U;
  std::size_t max_lexical_terms = 16U;
  std::size_t max_top_k = 12U;
  std::size_t max_context_projection_items = 8U;
  std::vector<std::string> allowed_domain_tags;
  std::vector<std::string> allowed_corpora;
  std::map<std::string, std::string> domain_tag_aliases;

  [[nodiscard]] bool has_consistent_values() const;
};

class QueryNormalizer {
 public:
  explicit QueryNormalizer(QueryNormalizePolicy policy);

  [[nodiscard]] NormalizeResult normalize(const KnowledgeQuery& query) const;

 private:
  [[nodiscard]] std::string canonicalize_text(std::string_view query_text,
                                              std::vector<std::string>& warnings) const;
  [[nodiscard]] std::vector<std::string> derive_lexical_terms(
      std::string_view text,
      std::vector<std::string>& warnings) const;
  [[nodiscard]] std::vector<std::string> normalize_tags(
      const std::vector<std::string>& tags,
      std::vector<std::string>& warnings,
      std::string_view dropped_invalid_warning,
      std::string_view filtered_allowlist_warning) const;
  [[nodiscard]] std::vector<std::string> normalize_allowed_corpora(
      const std::vector<std::string>& corpora,
      std::vector<std::string>& warnings) const;
    [[nodiscard]] std::optional<std::vector<std::string>> normalize_required_tags(
      const std::vector<std::string>& tags) const;
    [[nodiscard]] std::optional<std::string> normalize_required_language(
      const std::optional<std::string>& language) const;

  QueryNormalizePolicy policy_;
};

}  // namespace dasall::knowledge::query