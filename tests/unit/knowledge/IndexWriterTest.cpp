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
using dasall::knowledge::index::DenseSnapshotBuildResult;
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

void test_index_writer_builds_initial_snapshot_and_preserves_active_on_invalid_batch() {
  TempDirectory snapshots("dasall-index-writer-test");
  IndexReader reader;
  VersionLedger ledger(VersionLedgerDeps{
      .read_snapshot_checksum = [&reader](std::string_view snapshot_id) {
        return reader.read_snapshot_checksum(snapshot_id);
      },
      .ledger_path = snapshots.path / "ledger.txt",
  });

  std::int64_t now_ms = 1713657601000;
  bool catalog_refreshed = false;
  IndexWriterDeps deps;
  deps.snapshots_root = [&snapshots]() { return snapshots.path; };
  deps.now_ms = [&now_ms]() { return now_ms; };
  deps.refresh_catalog = [&reader, &catalog_refreshed](const dasall::knowledge::IndexManifest& manifest) {
    catalog_refreshed = true;
    const auto active_manifest = reader.current_manifest();
    assert_true(active_manifest.has_value(),
                "catalog refresh callback should observe an installed active manifest");
    assert_equal(manifest.snapshot_id, active_manifest->snapshot_id,
                 "catalog refresh callback should run after snapshot swap");
    return true;
  };

  IndexWriter writer(reader, ledger, deps);
  const auto batch = make_batch(
      'a', {make_chunk_record('b', 'c', "doclineage:source-001", "policy evidence baseline")});

  const auto report = writer.apply_update_batch(batch);
  assert_true(report.ok, "cold-start batch should build an active snapshot");
  assert_true(report.has_consistent_values(), "successful update report should be consistent");
  assert_true(report.manifest.has_value(), "successful update should expose a manifest");

  const auto manifest = reader.current_manifest();
  assert_true(manifest.has_value(), "reader should expose the new active manifest");
  assert_equal(report.snapshot_id, manifest->snapshot_id,
               "active snapshot id should match the writer report");
  assert_true(catalog_refreshed,
              "successful activation should trigger the catalog refresh seam");

  const auto checksum = reader.read_snapshot_checksum(report.snapshot_id);
  assert_true(checksum.has_value(), "active snapshot checksum should be queryable");

  const auto search_result = reader.search_sparse(make_request("policy"));
  assert_true(search_result.ok, "active snapshot should be searchable");
  assert_equal(1, static_cast<int>(search_result.rows.size()),
               "search should return the inserted chunk");
  assert_equal("chunk:" + make_hex('b'), search_result.rows.front().chunk_id,
               "returned chunk should match the inserted chunk id");

  const auto last_known_good = ledger.last_known_good();
  assert_true(last_known_good.has_value(), "activated snapshot should become last-known-good");
  assert_equal(report.snapshot_id, last_known_good->snapshot_id,
               "ledger should track the active snapshot as last-known-good");

  IndexUpdateBatch invalid_batch;
  invalid_batch.batch_id = "broken-batch";
  const auto invalid_report = writer.apply_update_batch(invalid_batch);
  assert_true(!invalid_report.ok, "invalid batch should fail closed");
  assert_true(invalid_report.error.has_value(), "invalid batch failure should expose error info");

  const auto manifest_after_invalid = reader.current_manifest();
  assert_true(manifest_after_invalid.has_value(),
              "invalid batch must not clear the existing active manifest");
  assert_equal(report.snapshot_id, manifest_after_invalid->snapshot_id,
               "invalid batch must not replace the active snapshot");
}

void test_index_writer_builds_dense_snapshot_from_seeded_effective_chunks() {
  TempDirectory snapshots("dasall-index-writer-dense-seeded-test");
  IndexReader reader;
  VersionLedger ledger(VersionLedgerDeps{
      .read_snapshot_checksum = [&reader](std::string_view snapshot_id) {
        return reader.read_snapshot_checksum(snapshot_id);
      },
      .ledger_path = snapshots.path / "ledger.txt",
  });

  std::int64_t now_ms = 1713657601000;
  std::vector<std::vector<std::string>> dense_request_chunk_ids;
  IndexWriterDeps deps;
  deps.snapshots_root = [&snapshots]() { return snapshots.path; };
  deps.now_ms = [&now_ms]() { return now_ms; };
  deps.build_dense_snapshot = [&dense_request_chunk_ids](const auto& request) {
    std::vector<std::string> chunk_ids;
    for (const auto& chunk : request.chunk_records) {
      chunk_ids.push_back(chunk.chunk_id);
    }
    dense_request_chunk_ids.push_back(std::move(chunk_ids));

    DenseSnapshotBuildResult result;
    result.ok = true;
    return result;
  };

  IndexWriter writer(reader, ledger, deps);
  auto initial_batch = make_batch(
      'd', {make_chunk_record('e', 'f', "doclineage:source-002", "seeded dense policy")});
  initial_batch.vector_enabled = true;

  const auto initial_report = writer.apply_update_batch(initial_batch);
  assert_true(initial_report.ok, "initial vector batch should build a snapshot");

  auto seeded_batch = make_batch('1', {});
  seeded_batch.vector_enabled = true;
  now_ms += 1000;
  const auto seeded_report = writer.apply_update_batch(seeded_batch);
  assert_true(seeded_report.ok, "seeded vector batch should build a snapshot");

  assert_equal(2, static_cast<int>(dense_request_chunk_ids.size()),
               "dense builder should be called for both vector snapshots");
  assert_equal(1, static_cast<int>(dense_request_chunk_ids.front().size()),
               "initial dense snapshot should receive inserted chunks");
  assert_equal(1, static_cast<int>(dense_request_chunk_ids.back().size()),
               "seeded dense snapshot should receive effective chunks copied from active lexical DB");
  assert_equal(dense_request_chunk_ids.front().front(), dense_request_chunk_ids.back().front(),
               "seeded dense snapshot should rebuild the active chunk set, not an empty delta");
}

}  // namespace

int main() {
  try {
    test_index_writer_builds_initial_snapshot_and_preserves_active_on_invalid_batch();
    test_index_writer_builds_dense_snapshot_from_seeded_effective_chunks();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}