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
using dasall::knowledge::index::VersionLedgerEntry;
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

void test_index_writer_rolls_back_to_previous_snapshot_when_activation_fails() {
  TempDirectory snapshots("dasall-index-writer-recovery-test");
  IndexReader reader;
  VersionLedger ledger(VersionLedgerDeps{
      .read_snapshot_checksum = [&reader](std::string_view snapshot_id) {
        return reader.read_snapshot_checksum(snapshot_id);
      },
  });

  std::int64_t now_ms = 1713657601000;
  int activation_count = 0;

  IndexWriterDeps deps;
  deps.snapshots_root = [&snapshots]() { return snapshots.path; };
  deps.now_ms = [&now_ms]() { return now_ms; };
  deps.record_candidate = [&ledger](const VersionLedgerEntry& entry) {
    return ledger.record_candidate(entry);
  };
  deps.mark_active = [&ledger, &activation_count](std::string_view snapshot_id,
                                                  std::int64_t activated_at) {
    ++activation_count;
    if (activation_count == 2) {
      return false;
    }
    return ledger.mark_active(snapshot_id, activated_at);
  };

  IndexWriter writer(reader, ledger, deps);
  const std::string lineage_id = "doclineage:source-003";

  const auto first_report = writer.apply_update_batch(
      make_batch('a', {make_chunk_record('b', 'c', lineage_id, "stable policy baseline")}));
  assert_true(first_report.ok, "initial batch should build the first active snapshot");
  assert_true(first_report.manifest.has_value(), "initial report should expose manifest");

  now_ms += 1000;
  const auto failed_report = writer.apply_update_batch(make_batch(
      'd', {make_chunk_record('e', 'f', lineage_id, "candidate policy upgrade")}, {lineage_id}));
  assert_true(!failed_report.ok, "activation failure should surface as failed update report");
  assert_true(failed_report.error.has_value(), "failed update should expose error info");

  const auto active_manifest = reader.current_manifest();
  assert_true(active_manifest.has_value(), "reader should still expose a last-known-good manifest");
  assert_equal(first_report.snapshot_id, active_manifest->snapshot_id,
               "activation failure must roll back to the previous active snapshot");

  const auto stable_result = reader.search_sparse(make_request("stable"));
  assert_true(stable_result.ok, "rolled-back snapshot should remain searchable");
  assert_equal(1, static_cast<int>(stable_result.rows.size()),
               "previous snapshot row should remain visible after rollback");

  const auto candidate_result = reader.search_sparse(make_request("candidate"));
  assert_true(candidate_result.ok, "failed candidate search should still return a valid envelope");
  assert_equal(0, static_cast<int>(candidate_result.rows.size()),
               "failed candidate snapshot must not stay active after rollback");

  const auto last_known_good = ledger.last_known_good();
  assert_true(last_known_good.has_value(), "ledger should keep the previous active snapshot as LKG");
  assert_equal(first_report.snapshot_id, last_known_good->snapshot_id,
               "ledger last-known-good should remain the previous snapshot");
}

}  // namespace

int main() {
  try {
    test_index_writer_rolls_back_to_previous_snapshot_when_activation_fails();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}