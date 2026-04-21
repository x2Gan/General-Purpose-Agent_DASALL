#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "index/IndexReader.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::IndexManifest;
using dasall::knowledge::index::IndexReader;
using dasall::knowledge::index::IndexSnapshot;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::knowledge::retrieve::SparseIndexSearchResult;
using dasall::knowledge::retrieve::SparseQueryExpression;
using dasall::knowledge::retrieve::SparseSearchRow;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] IndexManifest make_manifest(std::string snapshot_id,
                                          std::int64_t built_at,
                                          std::int64_t effective_at) {
  IndexManifest manifest;
  manifest.tokenizer_profile = "porter unicode61 remove_diacritics 1";
  manifest.snapshot_id = std::move(snapshot_id);
  manifest.built_at = built_at;
  manifest.effective_at = effective_at;
  manifest.document_count = 1U;
  manifest.chunk_count = 1U;
  return manifest;
}

[[nodiscard]] SparseIndexSearchRequest make_request() {
  SparseIndexSearchRequest request;
  request.expression.match_expression = "\"policy\"";
  request.expression.lexical_terms = {"policy"};
  request.allowed_corpus_ids = {"adr_normative"};
  request.required_tags = {"normative"};
  request.required_language = "zh-CN";
  request.minimum_authority_level = AuthorityLevel::Normative;
  request.top_k = 3U;
  return request;
}

[[nodiscard]] SparseSearchRow make_row(std::string chunk_id) {
  SparseSearchRow row;
  row.corpus_id = "adr_normative";
  row.document_id = "adr-0001";
  row.chunk_id = std::move(chunk_id);
  row.score = 0.75F;
  row.chunk_text = "policy evidence";
  row.citation_ref = "ADR-0001#policy";
  row.updated_at = 1713657600000;
  row.authority_level = AuthorityLevel::Normative;
  row.language = "zh-CN";
  row.tags = {"normative"};
  return row;
}

void test_index_reader_surfaces_manifest_and_search_results_from_active_snapshot() {
  const auto request = make_request();
  auto snapshot = std::make_shared<IndexSnapshot>();
  snapshot->manifest = make_manifest("snapshot-001", 100, 120);
  snapshot->checksum = "checksum-001";
  snapshot->search = [&request](const SparseIndexSearchRequest& incoming_request) {
    assert_true(incoming_request.has_consistent_values(),
                "active snapshot should receive a consistent sparse request");
    assert_equal(request.expression.match_expression, incoming_request.expression.match_expression,
                 "IndexReader should forward the lexical match expression unchanged");

    SparseIndexSearchResult result;
    result.ok = true;
    result.rows = {make_row("chunk-001")};
    return result;
  };

  IndexReader reader(snapshot);
  const auto manifest = reader.current_manifest();
  assert_true(manifest.has_value(),
              "IndexReader should expose the manifest of the active snapshot");
  assert_equal("snapshot-001", manifest->snapshot_id,
               "current_manifest should return the active snapshot id");

  const auto checksum = reader.read_snapshot_checksum("snapshot-001");
  assert_true(checksum.has_value(),
              "IndexReader should surface the checksum of the active snapshot");
  assert_equal("checksum-001", *checksum,
               "active checksum query should match the installed snapshot checksum");

  const auto search_result = reader.search_sparse(request);
  assert_true(search_result.ok,
              "IndexReader should return the active snapshot search result when available");
  assert_true(search_result.has_consistent_values(),
              "IndexReader should preserve the sparse search result shape");
  assert_equal(1, static_cast<int>(search_result.rows.size()),
               "active snapshot search should surface its matching rows");
  assert_equal("chunk-001", search_result.rows.front().chunk_id,
               "IndexReader should return rows from the active snapshot callback");
}

}  // namespace

int main() {
  try {
    test_index_reader_surfaces_manifest_and_search_results_from_active_snapshot();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}