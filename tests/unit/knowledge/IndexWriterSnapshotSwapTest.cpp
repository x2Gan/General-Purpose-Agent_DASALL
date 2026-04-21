#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "index/IndexWriter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::index::IndexReader;
using dasall::knowledge::index::IndexWriter;
using dasall::knowledge::index::IndexWriterDeps;
using dasall::knowledge::index::VersionLedger;
using dasall::knowledge::index::VersionLedgerDeps;
using dasall::knowledge::ingest::ChunkRecord;
using dasall::knowledge::ingest::IndexUpdateBatch;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct TempDirectory {
  explicit TempDirectory(std::string name)
      : path(std::filesystem::temp_directory_path() / std::move(name)) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path path;
};

[[nodiscard]] std::string make_hex(char value) {
  return std::string(64U, value);
}

[[nodiscard]] ChunkRecord make_chunk_record(char chunk_hex,
                                            char document_hex,
                                            std::string lineage_id,
                                            std::string text) {
  ChunkRecord record;
  record.chunk_id = "chunk:" + make_hex(chunk_hex);
  record.document_id = "document:" + make_hex(document_hex);
  record.corpus_id = "adr_normative";
  record.source_id = "source-" + lineage_id;
  record.source_uri = "/knowledge/" + lineage_id + ".md";
  record.chunk_text = std::move(text);
  record.version = "v1";
  record.updated_at_ms = 1713657600000;
  record.authority_level = AuthorityLevel::Normative;
  record.language = "zh-CN";
  record.token_estimate = 6U;
  record.span_begin = 0U;
  record.span_end = record.chunk_text.size();
  record.citation_ref = record.source_uri + "#char=0-" + std::to_string(record.span_end);
  record.tags = {"policy"};
  record.metadata = {
      {"document_class", "adr"},
      {"section_path", "policy/0"},
      {"document_lineage_id", std::move(lineage_id)},
  };
  return record;
}

[[nodiscard]] IndexUpdateBatch make_batch(char batch_hex,
                                          std::vector<ChunkRecord> chunk_records,
                                          std::vector<std::string> removed_document_ids = {}) {
  IndexUpdateBatch batch;
  batch.batch_id = "batch:" + make_hex(batch_hex);
  batch.chunk_records = std::move(chunk_records);
  batch.removed_document_ids = std::move(removed_document_ids);
  return batch;
}

[[nodiscard]] SparseIndexSearchRequest make_request(std::string term) {
  SparseIndexSearchRequest request;
  request.expression.match_expression = '"' + term + '"';
  request.expression.lexical_terms = {std::move(term)};
  request.allowed_corpus_ids = {"adr_normative"};
  request.required_tags = {"policy"};
  request.required_language = "zh-CN";
  request.minimum_authority_level = AuthorityLevel::Normative;
  request.top_k = 4U;
  return request;
}

void test_index_writer_swaps_active_snapshot_after_remove_by_lineage_refresh() {
  TempDirectory snapshots("dasall-index-writer-swap-test");
  IndexReader reader;
  VersionLedger ledger(VersionLedgerDeps{
      .read_snapshot_checksum = [&reader](std::string_view snapshot_id) {
        return reader.read_snapshot_checksum(snapshot_id);
      },
  });

  std::int64_t now_ms = 1713657601000;
  IndexWriterDeps deps;
  deps.snapshots_root = [&snapshots]() { return snapshots.path; };
  deps.now_ms = [&now_ms]() { return now_ms; };
  IndexWriter writer(reader, ledger, deps);

  const std::string lineage_id = "doclineage:source-002";
  const auto first_report = writer.apply_update_batch(
      make_batch('a', {make_chunk_record('b', 'c', lineage_id, "alpha policy baseline")}));
  assert_true(first_report.ok, "initial batch should build an active snapshot");

  now_ms += 1000;
  const auto second_report = writer.apply_update_batch(make_batch(
      'd', {make_chunk_record('e', 'f', lineage_id, "beta policy upgrade")}, {lineage_id}));
  assert_true(second_report.ok, "replacement batch should build and activate a new snapshot");
  assert_true(second_report.snapshot_id != first_report.snapshot_id,
              "replacement batch should create a distinct snapshot id");

  const auto alpha_result = reader.search_sparse(make_request("alpha"));
  assert_true(alpha_result.ok, "old query should still return a valid result envelope");
  assert_equal(0, static_cast<int>(alpha_result.rows.size()),
               "old chunk should be removed after lineage replacement");

  const auto beta_result = reader.search_sparse(make_request("beta"));
  assert_true(beta_result.ok, "new snapshot should be searchable");
  assert_equal(1, static_cast<int>(beta_result.rows.size()),
               "replacement chunk should be visible after swap");
  assert_equal("chunk:" + make_hex('e'), beta_result.rows.front().chunk_id,
               "new search result should come from the replacement snapshot");
}

}  // namespace

int main() {
  try {
    test_index_writer_swaps_active_snapshot_after_remove_by_lineage_refresh();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}