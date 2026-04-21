#include <exception>
#include <future>
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
  request.expression.match_expression = "\"snapshot\"";
  request.expression.lexical_terms = {"snapshot"};
  request.allowed_corpus_ids = {"adr_normative"};
  request.required_tags = {"normative"};
  request.required_language = "zh-CN";
  request.minimum_authority_level = AuthorityLevel::Normative;
  request.top_k = 2U;
  return request;
}

[[nodiscard]] SparseIndexSearchResult make_result(std::string chunk_id) {
  SparseSearchRow row;
  row.corpus_id = "adr_normative";
  row.document_id = "adr-0001";
  row.chunk_id = std::move(chunk_id);
  row.score = 0.5F;
  row.chunk_text = "snapshot data";
  row.citation_ref = "ADR-0001#snapshot";
  row.updated_at = 1713657600000;
  row.authority_level = AuthorityLevel::Normative;
  row.language = "zh-CN";
  row.tags = {"normative"};

  SparseIndexSearchResult result;
  result.ok = true;
  result.rows = {std::move(row)};
  return result;
}

void test_index_reader_keeps_inflight_reads_on_old_snapshot_after_swap() {
  const auto request = make_request();
  std::promise<void> first_search_started_promise;
  auto first_search_started = first_search_started_promise.get_future();
  std::promise<void> release_old_snapshot_promise;
  const auto release_old_snapshot = release_old_snapshot_promise.get_future().share();

  auto old_snapshot = std::make_shared<IndexSnapshot>();
  old_snapshot->manifest = make_manifest("snapshot-001", 100, 120);
  old_snapshot->checksum = "checksum-001";
  old_snapshot->search = [&first_search_started_promise, release_old_snapshot](
                             const SparseIndexSearchRequest&) {
    first_search_started_promise.set_value();
    release_old_snapshot.wait();
    return make_result("chunk-old");
  };

  auto new_snapshot = std::make_shared<IndexSnapshot>();
  new_snapshot->manifest = make_manifest("snapshot-002", 200, 240);
  new_snapshot->checksum = "checksum-002";
  new_snapshot->search = [](const SparseIndexSearchRequest&) { return make_result("chunk-new"); };

  IndexReader reader(old_snapshot);
  auto inflight_read = std::async(std::launch::async, [&reader, request] {
    return reader.search_sparse(request);
  });

  first_search_started.wait();
  assert_true(reader.swap_active_snapshot(new_snapshot),
              "IndexReader should atomically accept a new active snapshot");

  const auto new_search_result = reader.search_sparse(request);
  assert_true(new_search_result.ok,
              "new reads should observe the swapped-in snapshot immediately");
  assert_equal("chunk-new", new_search_result.rows.front().chunk_id,
               "reads after swap should use the new snapshot");

  release_old_snapshot_promise.set_value();
  const auto old_search_result = inflight_read.get();
  assert_true(old_search_result.ok,
              "inflight read should still complete successfully after swap");
  assert_equal("chunk-old", old_search_result.rows.front().chunk_id,
               "inflight read should stay on the previously loaded snapshot");

  const auto manifest = reader.current_manifest();
  assert_true(manifest.has_value(),
              "current_manifest should move to the new active snapshot after swap");
  assert_equal("snapshot-002", manifest->snapshot_id,
               "manifest should expose the swapped-in snapshot id");
}

}  // namespace

int main() {
  try {
    test_index_reader_keeps_inflight_reads_on_old_snapshot_after_swap();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}