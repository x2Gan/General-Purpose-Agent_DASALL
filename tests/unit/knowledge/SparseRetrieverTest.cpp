#include <sqlite3.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "retrieve/SparseRetriever.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::KnowledgeErrorCode;
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

void execute_sql(sqlite3* connection, const std::string& sql) {
  char* error_message = nullptr;
  const int sqlite_status =
      sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &error_message);
  if (sqlite_status != SQLITE_OK) {
    const std::string message = error_message != nullptr
                                    ? error_message
                                    : "failed to execute sqlite statement";
    sqlite3_free(error_message);
    throw std::runtime_error(message);
  }

  sqlite3_free(error_message);
}

[[nodiscard]] std::string encode_tags(const std::vector<std::string>& tags) {
  std::string encoded = "|";
  for (const auto& tag : tags) {
    encoded += tag;
    encoded.push_back('|');
  }
  return encoded;
}

[[nodiscard]] std::vector<std::string> decode_tags(const std::string& encoded_tags) {
  std::vector<std::string> tags;
  std::string current_tag;
  for (const char character : encoded_tags) {
    if (character == '|') {
      if (!current_tag.empty()) {
        tags.push_back(current_tag);
        current_tag.clear();
      }
      continue;
    }

    current_tag.push_back(character);
  }
  if (!current_tag.empty()) {
    tags.push_back(std::move(current_tag));
  }
  return tags;
}

class SqliteSparseIndexFixture {
 public:
  SqliteSparseIndexFixture() {
    if (sqlite3_open(":memory:", &connection_) != SQLITE_OK) {
      throw std::runtime_error("failed to open sqlite in-memory database");
    }

    execute_sql(connection_,
                "CREATE VIRTUAL TABLE chunks_fts USING fts5("
                "corpus_id UNINDEXED,"
                "document_id UNINDEXED,"
                "chunk_id UNINDEXED,"
                "chunk_text,"
                "citation_ref UNINDEXED,"
                "updated_at UNINDEXED,"
                "authority_level UNINDEXED,"
                "language UNINDEXED,"
                "tags UNINDEXED,"
                "tokenize='porter unicode61 remove_diacritics 1'"
                ");");
  }

  ~SqliteSparseIndexFixture() {
    if (connection_ != nullptr) {
      sqlite3_close(connection_);
    }
  }

  void insert_row(std::string corpus_id,
                  std::string document_id,
                  std::string chunk_id,
                  std::string chunk_text,
                  std::string citation_ref,
                  std::int64_t updated_at,
                  AuthorityLevel authority_level,
                  std::string language,
                  std::vector<std::string> tags) {
    constexpr auto insert_sql =
        "INSERT INTO chunks_fts("
        "corpus_id, document_id, chunk_id, chunk_text, citation_ref, updated_at, authority_level, language, tags"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, insert_sql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("failed to prepare sparse retriever fixture insert");
    }

    sqlite3_bind_text(statement, 1, corpus_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, document_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 4, chunk_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 5, citation_ref.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 6, updated_at);
    sqlite3_bind_int(statement, 7, static_cast<int>(authority_level));
    sqlite3_bind_text(statement, 8, language.c_str(), -1, SQLITE_TRANSIENT);
    const auto encoded_tags = encode_tags(tags);
    sqlite3_bind_text(statement, 9, encoded_tags.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
      sqlite3_finalize(statement);
      throw std::runtime_error("failed to insert sparse retriever fixture row");
    }

    sqlite3_finalize(statement);
  }

  [[nodiscard]] SparseIndexSearchResult search(
      const SparseIndexSearchRequest& request) const {
    std::string sql =
        "SELECT corpus_id, document_id, chunk_id, chunk_text, citation_ref, updated_at, authority_level, language, tags, bm25(chunks_fts) "
        "FROM chunks_fts WHERE chunks_fts MATCH ?1";

    int bind_index = 2;
    if (!request.allowed_corpus_ids.empty()) {
      sql += " AND corpus_id IN (";
      for (std::size_t index = 0U; index < request.allowed_corpus_ids.size(); ++index) {
        if (index > 0U) {
          sql += ", ";
        }
        sql += "?" + std::to_string(bind_index++);
      }
      sql += ")";
    }

    sql += " AND CAST(authority_level AS INTEGER) <= ?" + std::to_string(bind_index++);

    if (request.required_language.has_value()) {
      sql += " AND language = ?" + std::to_string(bind_index++);
    }

    for (std::size_t index = 0U; index < request.required_tags.size(); ++index) {
      sql += " AND instr(tags, ?" + std::to_string(bind_index++) + ") > 0";
    }

    sql += " ORDER BY bm25(chunks_fts) LIMIT ?" + std::to_string(bind_index++);

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
      SparseIndexSearchResult result;
      result.ok = false;
      result.error = dasall::knowledge::make_knowledge_error_info(
          KnowledgeErrorCode::IndexUnavailable,
          "failed to prepare sparse index sqlite search statement",
          "sqlite_sparse_fixture.search");
      return result;
    }

    int parameter_index = 1;
    sqlite3_bind_text(statement,
                      parameter_index++,
                      request.expression.match_expression.c_str(),
                      -1,
                      SQLITE_TRANSIENT);

    for (const auto& corpus_id : request.allowed_corpus_ids) {
      sqlite3_bind_text(statement,
                        parameter_index++,
                        corpus_id.c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(statement,
                     parameter_index++,
                     static_cast<int>(request.minimum_authority_level));

    if (request.required_language.has_value()) {
      sqlite3_bind_text(statement,
                        parameter_index++,
                        request.required_language->c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    for (const auto& required_tag : request.required_tags) {
      const auto encoded_tag = "|" + required_tag + "|";
      sqlite3_bind_text(statement,
                        parameter_index++,
                        encoded_tag.c_str(),
                        -1,
                        SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(statement,
                     parameter_index++,
                     static_cast<int>(request.top_k));

    SparseIndexSearchResult result;
    result.ok = true;
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const auto score = std::max(
          0.0F,
          static_cast<float>(-sqlite3_column_double(statement, 9)));
      const auto* language_text =
          reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
      const auto* tags_text =
          reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
      result.rows.push_back(SparseSearchRow{
          .corpus_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
          .document_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
          .chunk_id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
          .score = score,
          .chunk_text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3)),
          .citation_ref = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4)),
          .updated_at = sqlite3_column_int64(statement, 5),
          .authority_level = static_cast<AuthorityLevel>(sqlite3_column_int(statement, 6)),
          .language = language_text != nullptr ? std::optional<std::string>{language_text}
                                               : std::nullopt,
          .tags = tags_text != nullptr ? decode_tags(tags_text) : std::vector<std::string>{},
      });
    }

    sqlite3_finalize(statement);
    return result;
  }

 private:
  sqlite3* connection_ = nullptr;
};

[[nodiscard]] SparseRetrieveRequest make_request() {
  return SparseRetrieveRequest{
      .normalized_query = NormalizedQuery{
          .request_id = "req-sparse-013-01",
          .normalized_text = "owner boundary",
          .lexical_terms = {"owner", "boundary"},
          .domain_tags = {"architecture"},
          .allowed_corpora = {},
          .query_kind = KnowledgeQueryKind::FactLookup,
          .top_k = 2U,
          .max_context_projection_items = 2U,
          .prefer_exact_match = true,
          .allow_stale = false,
          .warnings = {},
      },
      .plan = RetrievalPlan{
          .mode = RetrievalMode::LexicalOnly,
          .corpus_ids = {"architecture-reference"},
          .sparse_top_k = 2U,
          .dense_top_k = 0U,
          .allow_partial_results = false,
          .allow_stale_snapshot = false,
          .max_projection_items = 2U,
          .route_reason_codes = {"mode_lexical_only"},
      },
      .required_language = std::string("en"),
  };
}

void test_sparse_retriever_builds_sqlite_expression_and_returns_hits() {
  SqliteSparseIndexFixture fixture;
  fixture.insert_row(
      "architecture-reference",
      "doc-owner-boundary",
      "chunk-owner-boundary",
      "Owner boundary stays inside runtime orchestrator. Evidence is scoped to ADR-008.",
      "docs/architecture/adr-008.md#L12",
      1200,
      AuthorityLevel::Reference,
      "en",
      {"architecture", "adr"});

  std::optional<SparseIndexSearchRequest> captured_request;
  SparseRetriever retriever(
      SparseRetrieverDeps{
          .search_index = [&](const SparseIndexSearchRequest& request) {
            captured_request = request;
            return fixture.search(request);
          },
      },
      SparseRetrieverPolicy{
          .sentence_window = 0U,
          .max_snippet_characters = 180U,
      });

  const auto result = retriever.retrieve(make_request());

  assert_true(captured_request.has_value(),
              "sqlite-backed sparse retriever should invoke the search seam");
  assert_true(captured_request->expression.exact_phrase_preferred,
              "prefer_exact_match should upgrade the expression to include an exact phrase branch");
  assert_true(captured_request->expression.match_expression.find("\"owner boundary\"") !=
                  std::string::npos,
              "query expression should contain the quoted exact phrase branch");
  assert_true(result.has_consistent_values(),
              "sqlite-backed sparse retriever result should satisfy invariants");
  assert_true(result.ok,
              "sqlite-backed sparse retriever should treat a lexical match as success");
  assert_equal(1, static_cast<int>(result.hits.size()),
               "sqlite-backed sparse retriever should return the matching chunk");
  assert_equal(std::string("chunk-owner-boundary"), result.hits.front().chunk_id,
               "sqlite-backed sparse retriever should preserve chunk identity");
  assert_true(result.hits.front().raw_snippet.find("Owner boundary stays inside runtime orchestrator.") !=
                  std::string::npos,
              "sparse retriever should derive a snippet from the matched sentence");
}

void test_sparse_retriever_reports_missing_index_seam_as_explicit_failure() {
  SparseRetriever retriever(SparseRetrieverDeps{});

  const auto result = retriever.retrieve(make_request());

  assert_true(result.has_consistent_values(),
              "missing sparse index seam should still return a structured error");
  assert_true(!result.ok,
              "missing sparse index seam should not be reported as an empty-hit success");
  assert_equal(static_cast<int>(KnowledgeErrorCode::IndexUnavailable),
               *result.error->details.code,
               "missing sparse index seam should map to IndexUnavailable");
}

}  // namespace

int main() {
  try {
    test_sparse_retriever_builds_sqlite_expression_and_returns_hits();
    test_sparse_retriever_reports_missing_index_seam_as_explicit_failure();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}