#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::query::NormalizedQuery;
using dasall::knowledge::query::RetrievalPlan;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::knowledge::retrieve::SparseIndexSearchResult;
using dasall::knowledge::retrieve::SparseRetrieveRequest;
using dasall::knowledge::retrieve::SparseRetriever;
using dasall::knowledge::retrieve::SparseRetrieverDeps;
using dasall::knowledge::retrieve::SparseSearchRow;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] SparseRetrieveRequest make_policy_request() {
  return SparseRetrieveRequest{
      .normalized_query = NormalizedQuery{
          .request_id = "req-sparse-013-filter",
          .normalized_text = "policy override",
          .lexical_terms = {"policy", "override"},
          .domain_tags = {"policy"},
          .allowed_corpora = {},
          .required_tags = {"policy"},
          .query_kind = KnowledgeQueryKind::PolicyEvidence,
          .top_k = 4U,
          .max_context_projection_items = 3U,
          .prefer_exact_match = false,
          .allow_stale = false,
          .warnings = {},
      },
      .plan = RetrievalPlan{
          .mode = RetrievalMode::LexicalOnly,
          .corpus_ids = {"policy-handbook"},
          .sparse_top_k = 4U,
          .dense_top_k = 0U,
          .allow_partial_results = false,
          .allow_stale_snapshot = false,
          .max_projection_items = 3U,
          .route_reason_codes = {"mode_lexical_only"},
      },
      .required_language = std::string("en"),
  };
}

[[nodiscard]] SparseSearchRow make_row(std::string chunk_id,
                                       AuthorityLevel authority_level,
                                       std::optional<std::string> language,
                                       std::vector<std::string> tags) {
  return SparseSearchRow{
      .corpus_id = "policy-handbook",
      .document_id = "doc-" + chunk_id,
      .chunk_id = std::move(chunk_id),
      .score = 0.8F,
      .chunk_text = "Policy override requires explicit approval before rollout.",
      .citation_ref = "docs/policy/rollout.md#L20",
      .updated_at = 4200,
      .authority_level = authority_level,
      .language = std::move(language),
      .tags = std::move(tags),
  };
}

void test_sparse_retriever_applies_metadata_language_and_authority_filters() {
  std::optional<SparseIndexSearchRequest> captured_request;
  SparseRetriever retriever(SparseRetrieverDeps{
      .search_index = [&](const SparseIndexSearchRequest& request) {
        captured_request = request;
        SparseIndexSearchResult result;
        result.ok = true;
        result.rows = {
            make_row("chunk-normative-en", AuthorityLevel::Normative, std::string("en"), {"policy", "safety"}),
            make_row("chunk-reference-en", AuthorityLevel::Reference, std::string("en"), {"policy", "safety"}),
            make_row("chunk-normative-zh", AuthorityLevel::Normative, std::string("zh"), {"policy", "safety"}),
            make_row("chunk-normative-runtime", AuthorityLevel::Normative, std::string("en"), {"runtime"}),
        };
        return result;
      },
  });

  const auto result = retriever.retrieve(make_policy_request());

  assert_true(captured_request.has_value(),
              "filter path should forward a structured search request to the index seam");
  assert_equal(1, static_cast<int>(captured_request->required_tags.size()),
               "policy query should propagate its metadata tag filter");
  assert_true(captured_request->required_language == std::optional<std::string>{"en"},
              "filter path should forward the requested language");
  assert_true(captured_request->minimum_authority_level == AuthorityLevel::Normative,
              "policy evidence query should require normative authority");
  assert_true(result.has_consistent_values(),
              "filtered sparse result should remain internally consistent");
  assert_true(result.ok,
              "filter-only pruning should still be treated as a successful lexical search");
  assert_equal(1, static_cast<int>(result.hits.size()),
               "only the row satisfying authority, language and metadata filters should survive");
  assert_equal(std::string("chunk-normative-en"), result.hits.front().chunk_id,
               "filter path should keep the unique matching row");
}

void test_sparse_retriever_treats_zero_hits_after_filtering_as_legal_success() {
  SparseRetriever retriever(SparseRetrieverDeps{
      .search_index = [](const SparseIndexSearchRequest&) {
        SparseIndexSearchResult result;
        result.ok = true;
        result.rows = {};
        return result;
      },
  });

  const auto result = retriever.retrieve(make_policy_request());

  assert_true(result.has_consistent_values(),
              "zero-hit sparse result should still satisfy the public result shape");
  assert_true(result.ok,
              "0-hit filter conflicts should be legal lexical success, not provider failure");
  assert_true(result.hits.empty(),
              "0-hit lexical success should return an empty hit list");
}

}  // namespace

int main() {
  try {
    test_sparse_retriever_applies_metadata_language_and_authority_filters();
    test_sparse_retriever_treats_zero_hits_after_filtering_as_legal_success();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}