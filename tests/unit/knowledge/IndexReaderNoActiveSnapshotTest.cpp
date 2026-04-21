#include <exception>
#include <iostream>
#include <string>

#include "KnowledgeErrors.h"
#include "index/IndexReader.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::index::IndexReader;
using dasall::knowledge::retrieve::SparseIndexSearchRequest;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] SparseIndexSearchRequest make_request() {
  SparseIndexSearchRequest request;
  request.expression.match_expression = "\"bootstrap\"";
  request.expression.lexical_terms = {"bootstrap"};
  request.allowed_corpus_ids = {"adr_normative"};
  request.required_tags = {"normative"};
  request.required_language = "zh-CN";
  request.minimum_authority_level = AuthorityLevel::Normative;
  request.top_k = 1U;
  return request;
}

void test_index_reader_reports_missing_active_snapshot_as_explicit_failure() {
  IndexReader reader;
  const auto request = make_request();

  assert_true(!reader.current_manifest().has_value(),
              "reader without an active snapshot should not expose a manifest");
  assert_true(!reader.read_snapshot_checksum("missing-snapshot").has_value(),
              "checksum lookup should fail when no active snapshot is installed");

  const auto search_result = reader.search_sparse(request);
  assert_true(!search_result.ok,
              "reader without an active snapshot should fail search explicitly");
  assert_true(search_result.has_consistent_values(),
              "explicit no-active-snapshot failure should still satisfy result shape");
  assert_true(search_result.error.has_value(),
              "no-active-snapshot failure should project structured error info");
  assert_equal(static_cast<int>(KnowledgeErrorCode::IndexUnavailable),
               search_result.error->details.code.value_or(-1),
               "missing active snapshot should map to IndexUnavailable");
  assert_equal("active_snapshot_missing", search_result.error->source_ref.ref_id,
               "missing active snapshot should expose a stable source ref id");
}

}  // namespace

int main() {
  try {
    test_index_reader_reports_missing_active_snapshot_as_explicit_failure();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}