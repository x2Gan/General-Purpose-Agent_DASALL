#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "KnowledgeErrors.h"
#include "query/CorpusRouter.h"
#include "query/QueryNormalizer.h"
#include "retrieve/RecallTypes.h"

namespace dasall::knowledge::retrieve {

struct SparseQueryExpression {
  std::string match_expression;
  std::vector<std::string> lexical_terms;
  bool exact_phrase_preferred = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SparseSearchRow {
  std::string corpus_id;
  std::string document_id;
  std::string chunk_id;
  float score = 0.0F;
  std::string chunk_text;
  std::string citation_ref;
  std::int64_t updated_at = 0;
  AuthorityLevel authority_level = AuthorityLevel::Reference;
  std::optional<std::string> language;
  std::vector<std::string> tags;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SparseIndexSearchRequest {
  SparseQueryExpression expression;
  std::vector<std::string> allowed_corpus_ids;
  std::vector<std::string> required_tags;
  std::optional<std::string> required_language;
  AuthorityLevel minimum_authority_level = AuthorityLevel::Reference;
  std::size_t top_k = 0U;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SparseIndexSearchResult {
  bool ok = false;
  std::vector<SparseSearchRow> rows;
  std::vector<std::string> warnings;
  std::optional<dasall::contracts::ErrorInfo> error;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SparseRetrieveRequest {
  query::NormalizedQuery normalized_query;
  query::RetrievalPlan plan;
  std::optional<std::string> required_language;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SparseRetrieveResult {
  bool ok = false;
  std::vector<RecallHit> hits;
  std::vector<std::string> warnings;
  std::optional<dasall::contracts::ErrorInfo> error;

  [[nodiscard]] bool has_consistent_values() const;
};

struct SparseRetrieverDeps {
  std::function<SparseIndexSearchResult(const SparseIndexSearchRequest& request)>
      search_index;
};

struct SparseRetrieverPolicy {
  std::size_t sentence_window = 1U;
  std::size_t max_snippet_characters = 320U;

  [[nodiscard]] bool has_consistent_values() const;
};

class SparseRetriever {
 public:
  explicit SparseRetriever(SparseRetrieverDeps deps,
                           SparseRetrieverPolicy policy = {});

  [[nodiscard]] SparseRetrieveResult retrieve(
      const SparseRetrieveRequest& request) const;

 private:
  [[nodiscard]] SparseQueryExpression build_query_expression(
      const SparseRetrieveRequest& request) const;
  [[nodiscard]] std::vector<RecallHit> expand_sentence_window(
      const std::vector<SparseSearchRow>& rows,
      const SparseQueryExpression& expression) const;

  SparseRetrieverDeps deps_{};
  SparseRetrieverPolicy policy_{};
};

}  // namespace dasall::knowledge::retrieve