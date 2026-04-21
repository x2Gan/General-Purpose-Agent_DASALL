#include <exception>
#include <iostream>
#include <string>

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
using dasall::knowledge::retrieve::SparseRetrieverPolicy;
using dasall::knowledge::retrieve::SparseSearchRow;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] SparseRetrieveRequest make_window_request() {
  return SparseRetrieveRequest{
      .normalized_query = NormalizedQuery{
          .request_id = "req-sparse-013-window",
          .normalized_text = "owner boundary",
          .lexical_terms = {"owner", "boundary"},
          .domain_tags = {},
          .allowed_corpora = {},
          .query_kind = KnowledgeQueryKind::FactLookup,
          .top_k = 1U,
          .max_context_projection_items = 1U,
          .prefer_exact_match = false,
          .allow_stale = false,
          .warnings = {},
      },
      .plan = RetrievalPlan{
          .mode = RetrievalMode::LexicalOnly,
          .corpus_ids = {"architecture-reference"},
          .sparse_top_k = 1U,
          .dense_top_k = 0U,
          .allow_partial_results = false,
          .allow_stale_snapshot = false,
          .max_projection_items = 1U,
          .route_reason_codes = {"mode_lexical_only"},
      },
      .required_language = std::string("en"),
  };
}

void test_sparse_retriever_expands_anchor_sentence_with_one_sentence_window() {
  SparseRetriever retriever(
      SparseRetrieverDeps{
          .search_index = [](const SparseIndexSearchRequest&) {
            SparseIndexSearchResult result;
            result.ok = true;
            result.rows = {SparseSearchRow{
                .corpus_id = "architecture-reference",
                .document_id = "doc-owner-boundary",
                .chunk_id = "chunk-owner-boundary",
                .score = 0.9F,
                .chunk_text =
                    "Intro sentence describes the runtime scope. "
                    "Owner boundary stays inside runtime orchestrator. "
                    "Supporting evidence references ADR-008. "
                    "Final sentence should stay outside the selected window.",
                .citation_ref = "docs/architecture/adr-008.md#L18",
                .updated_at = 1500,
                .authority_level = AuthorityLevel::Reference,
                .language = std::string("en"),
                .tags = {"architecture"},
            }};
            return result;
          },
      },
      SparseRetrieverPolicy{
          .sentence_window = 1U,
          .max_snippet_characters = 220U,
      });

  const auto result = retriever.retrieve(make_window_request());

  assert_true(result.has_consistent_values(),
              "sentence-window expansion should preserve the public hit invariants");
  assert_true(result.ok,
              "sentence-window expansion should keep a normal lexical hit on the success path");
  assert_equal(1, static_cast<int>(result.hits.size()),
               "sentence-window test should return exactly one hit");
  const auto& snippet = result.hits.front().raw_snippet;
  assert_true(snippet.find("Intro sentence describes the runtime scope.") != std::string::npos,
              "sentence-window should include the preceding sentence around the anchor");
  assert_true(snippet.find("Owner boundary stays inside runtime orchestrator.") != std::string::npos,
              "sentence-window should retain the anchor sentence containing lexical terms");
  assert_true(snippet.find("Supporting evidence references ADR-008.") != std::string::npos,
              "sentence-window should include the following sentence inside the configured window");
  assert_true(snippet.find("Final sentence should stay outside the selected window.") == std::string::npos,
              "sentence-window should not leak sentences outside the configured radius");
}

}  // namespace

int main() {
  try {
    test_sparse_retriever_expands_anchor_sentence_with_one_sentence_window();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}