#include "retrieve/SparseRetriever.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace dasall::knowledge::retrieve {

namespace {

[[nodiscard]] AuthorityLevel minimum_authority_level(
    KnowledgeQueryKind query_kind) {
  switch (query_kind) {
    case KnowledgeQueryKind::PolicyEvidence:
      return AuthorityLevel::Normative;
    case KnowledgeQueryKind::DiagnosticContext:
      return AuthorityLevel::Advisory;
    case KnowledgeQueryKind::FactLookup:
    case KnowledgeQueryKind::ProcedureLookup:
      return AuthorityLevel::Reference;
    case KnowledgeQueryKind::MultiHop:
      return AuthorityLevel::Normative;
  }

  return AuthorityLevel::Reference;
}

[[nodiscard]] bool matches_authority_level(AuthorityLevel authority_level,
                                           KnowledgeQueryKind query_kind) {
  return static_cast<int>(authority_level) <=
         static_cast<int>(minimum_authority_level(query_kind));
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
  std::size_t begin = 0U;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }

  return std::string(value.substr(begin, end - begin));
}

[[nodiscard]] std::string collapse_whitespace(std::string_view value) {
  std::string collapsed;
  collapsed.reserve(value.size());
  bool previous_was_space = false;
  for (const unsigned char character : value) {
    if (std::isspace(character) != 0) {
      if (!previous_was_space && !collapsed.empty()) {
        collapsed.push_back(' ');
      }
      previous_was_space = true;
      continue;
    }

    previous_was_space = false;
    collapsed.push_back(static_cast<char>(character));
  }

  if (!collapsed.empty() && collapsed.back() == ' ') {
    collapsed.pop_back();
  }

  return collapsed;
}

[[nodiscard]] std::string lower_ascii_copy(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const unsigned char character : value) {
    lowered.push_back(
        static_cast<char>(std::tolower(character)));
  }
  return lowered;
}

[[nodiscard]] std::vector<std::string> derive_terms(
    const query::NormalizedQuery& query) {
  if (!query.lexical_terms.empty()) {
    return query.lexical_terms;
  }

  std::vector<std::string> terms;
  std::string current_term;
  for (const unsigned char character : query.normalized_text) {
    if (std::isspace(character) != 0) {
      if (!current_term.empty()) {
        terms.push_back(current_term);
        current_term.clear();
      }
      continue;
    }

    current_term.push_back(static_cast<char>(character));
  }

  if (!current_term.empty()) {
    terms.push_back(std::move(current_term));
  }

  return terms;
}

[[nodiscard]] std::string escape_fts_literal(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 2U);
  for (const char character : value) {
    if (character == '"') {
      escaped.push_back('"');
    }
    escaped.push_back(character);
  }
  return escaped;
}

[[nodiscard]] std::string build_term_expression(
    const std::vector<std::string>& lexical_terms) {
  std::string expression;
  for (std::size_t index = 0U; index < lexical_terms.size(); ++index) {
    if (index > 0U) {
      expression += " AND ";
    }
    expression += '"';
    expression += escape_fts_literal(lexical_terms[index]);
    expression += '"';
  }
  return expression;
}

[[nodiscard]] std::string build_phrase_expression(
    const std::vector<std::string>& lexical_terms) {
  std::string phrase;
  for (std::size_t index = 0U; index < lexical_terms.size(); ++index) {
    if (index > 0U) {
      phrase.push_back(' ');
    }
    phrase += escape_fts_literal(lexical_terms[index]);
  }
  return '"' + phrase + '"';
}

[[nodiscard]] SparseRetrieveResult make_retrieve_error(
    KnowledgeErrorCode code,
    std::string message,
    std::string stage) {
  SparseRetrieveResult result;
  result.ok = false;
  result.error = make_knowledge_error_info(code,
                                           std::move(message),
                                           std::move(stage));
  return result;
}

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool contains_all_tags(const std::vector<std::string>& candidate_tags,
                                     const std::vector<std::string>& required_tags) {
  if (required_tags.empty()) {
    return true;
  }

  std::vector<std::string> lowered_tags;
  lowered_tags.reserve(candidate_tags.size());
  for (const auto& candidate_tag : candidate_tags) {
    lowered_tags.push_back(lower_ascii_copy(candidate_tag));
  }

  return std::all_of(required_tags.begin(), required_tags.end(), [&](const auto& required_tag) {
    return contains_string(lowered_tags, lower_ascii_copy(required_tag));
  });
}

[[nodiscard]] bool matches_language(
    const std::optional<std::string>& candidate_language,
    const std::optional<std::string>& required_language) {
  if (!required_language.has_value()) {
    return true;
  }

  return candidate_language.has_value() &&
         lower_ascii_copy(*candidate_language) ==
             lower_ascii_copy(*required_language);
}

[[nodiscard]] std::size_t utf8_sentence_delimiter_size(std::string_view text,
                                                       std::size_t index) {
  constexpr std::string_view delimiters[] = {"。", "！", "？", "；"};
  for (const auto delimiter : delimiters) {
    if (text.compare(index, delimiter.size(), delimiter) == 0) {
      return delimiter.size();
    }
  }

  return 0U;
}

[[nodiscard]] bool is_ascii_sentence_delimiter(char character) {
  switch (character) {
    case '.':
    case '!':
    case '?':
    case ';':
    case '\n':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::vector<std::string> split_sentences(std::string_view chunk_text) {
  std::vector<std::string> sentences;
  std::size_t sentence_begin = 0U;
  std::size_t index = 0U;

  while (index < chunk_text.size()) {
    const auto utf8_delimiter_size = utf8_sentence_delimiter_size(chunk_text, index);
    if (utf8_delimiter_size > 0U) {
      index += utf8_delimiter_size;
      auto sentence = trim_copy(chunk_text.substr(sentence_begin, index - sentence_begin));
      if (!sentence.empty()) {
        sentences.push_back(std::move(sentence));
      }
      sentence_begin = index;
      continue;
    }

    if (is_ascii_sentence_delimiter(chunk_text[index])) {
      ++index;
      auto sentence = trim_copy(chunk_text.substr(sentence_begin, index - sentence_begin));
      if (!sentence.empty()) {
        sentences.push_back(std::move(sentence));
      }
      sentence_begin = index;
      continue;
    }

    ++index;
  }

  if (sentence_begin < chunk_text.size()) {
    auto trailing_sentence = trim_copy(chunk_text.substr(sentence_begin));
    if (!trailing_sentence.empty()) {
      sentences.push_back(std::move(trailing_sentence));
    }
  }

  if (sentences.empty()) {
    auto fallback = trim_copy(chunk_text);
    if (!fallback.empty()) {
      sentences.push_back(std::move(fallback));
    }
  }

  return sentences;
}

[[nodiscard]] std::size_t find_anchor_sentence(
    const std::vector<std::string>& sentences,
    const std::vector<std::string>& lexical_terms) {
  std::size_t anchor_index = 0U;
  std::size_t best_score = 0U;
  for (std::size_t index = 0U; index < sentences.size(); ++index) {
    const auto lowered_sentence = lower_ascii_copy(sentences[index]);
    std::size_t sentence_score = 0U;
    for (const auto& lexical_term : lexical_terms) {
      if (!lexical_term.empty() &&
          lowered_sentence.find(lower_ascii_copy(lexical_term)) != std::string::npos) {
        ++sentence_score;
      }
    }

    if (sentence_score > best_score) {
      best_score = sentence_score;
      anchor_index = index;
    }
  }

  return anchor_index;
}

[[nodiscard]] std::string build_window_snippet(
    std::string_view chunk_text,
    const std::vector<std::string>& lexical_terms,
    const SparseRetrieverPolicy& policy) {
  auto sentences = split_sentences(chunk_text);
  if (sentences.empty()) {
    return {};
  }

  const auto anchor_index = find_anchor_sentence(sentences, lexical_terms);
  const auto window_begin =
      anchor_index > policy.sentence_window ? anchor_index - policy.sentence_window : 0U;
  const auto window_end = std::min(sentences.size(),
                                   anchor_index + policy.sentence_window + 1U);

  std::string snippet;
  for (std::size_t index = window_begin; index < window_end; ++index) {
    if (!snippet.empty()) {
      snippet.push_back(' ');
    }
    snippet += sentences[index];
  }

  snippet = collapse_whitespace(snippet);
  if (snippet.size() > policy.max_snippet_characters &&
      policy.max_snippet_characters > 3U) {
    snippet.resize(policy.max_snippet_characters - 3U);
    snippet += "...";
  }

  return snippet;
}

}  // namespace

bool SparseQueryExpression::has_consistent_values() const {
  return !match_expression.empty() && !lexical_terms.empty() &&
         detail::has_unique_values(lexical_terms);
}

bool SparseSearchRow::has_consistent_values() const {
  return !corpus_id.empty() && !document_id.empty() && !chunk_id.empty() &&
         score >= 0.0F && !chunk_text.empty() && !citation_ref.empty() &&
         updated_at >= 0 && (!language.has_value() || !language->empty()) &&
         detail::has_unique_values(tags);
}

bool SparseIndexSearchRequest::has_consistent_values() const {
  return expression.has_consistent_values() && !allowed_corpus_ids.empty() &&
         detail::has_unique_values(allowed_corpus_ids) &&
         detail::has_unique_values(required_tags) && top_k > 0U &&
         (!required_language.has_value() || !required_language->empty());
}

bool SparseIndexSearchResult::has_consistent_values() const {
  if (!detail::has_unique_values(warnings)) {
    return false;
  }

  if (ok) {
    return std::all_of(rows.begin(), rows.end(), [](const auto& row) {
             return row.has_consistent_values();
           }) &&
           !error.has_value();
  }

  return rows.empty() && error.has_value() && detail::has_error_shape(error);
}

bool SparseRetrieveRequest::has_consistent_values() const {
  return normalized_query.has_consistent_values() && plan.has_consistent_values() &&
         plan.mode != RetrievalMode::DenseOnly && plan.sparse_top_k > 0U &&
         (!required_language.has_value() || !required_language->empty());
}

bool SparseRetrieveResult::has_consistent_values() const {
  if (!detail::has_unique_values(warnings)) {
    return false;
  }

  if (ok) {
    return std::all_of(hits.begin(), hits.end(), [](const auto& hit) {
             return hit.has_consistent_values();
           }) &&
           !error.has_value();
  }

  return hits.empty() && error.has_value() && detail::has_error_shape(error);
}

bool SparseRetrieverPolicy::has_consistent_values() const {
  return max_snippet_characters > 0U;
}

SparseRetriever::SparseRetriever(SparseRetrieverDeps deps,
                                 SparseRetrieverPolicy policy)
    : deps_(std::move(deps)), policy_(std::move(policy)) {}

SparseRetrieveResult SparseRetriever::retrieve(
    const SparseRetrieveRequest& request) const {
  if (!request.has_consistent_values()) {
    return make_retrieve_error(KnowledgeErrorCode::InternalError,
                               "sparse retriever request is inconsistent",
                               "sparse_retriever.retrieve");
  }

  if (!policy_.has_consistent_values()) {
    return make_retrieve_error(KnowledgeErrorCode::InternalError,
                               "sparse retriever policy is inconsistent",
                               "sparse_retriever.retrieve");
  }

  if (!deps_.search_index) {
    return make_retrieve_error(KnowledgeErrorCode::IndexUnavailable,
                               "active lexical snapshot search seam is unavailable",
                               "sparse_retriever.retrieve");
  }

  const auto expression = build_query_expression(request);
  if (!expression.has_consistent_values()) {
    return make_retrieve_error(KnowledgeErrorCode::QueryValidationFailed,
                               "lexical query expression is empty after normalization",
                               "sparse_retriever.build_query_expression");
  }

  SparseIndexSearchRequest search_request{
      .expression = expression,
      .allowed_corpus_ids = request.plan.corpus_ids,
      .required_tags = request.normalized_query.domain_tags,
      .required_language = request.required_language,
      .minimum_authority_level =
          minimum_authority_level(request.normalized_query.query_kind),
      .top_k = request.plan.sparse_top_k,
  };

  if (!search_request.has_consistent_values()) {
    return make_retrieve_error(KnowledgeErrorCode::InternalError,
                               "sparse index search request is inconsistent",
                               "sparse_retriever.retrieve");
  }

  auto search_result = deps_.search_index(search_request);
  if (!search_result.has_consistent_values()) {
    return make_retrieve_error(KnowledgeErrorCode::InternalError,
                               "sparse index search result is inconsistent",
                               "sparse_retriever.search_index");
  }

  if (!search_result.ok) {
    SparseRetrieveResult result;
    result.ok = false;
    result.warnings = std::move(search_result.warnings);
    result.error = std::move(search_result.error);
    return result;
  }

  std::vector<SparseSearchRow> filtered_rows;
  filtered_rows.reserve(search_result.rows.size());
  for (const auto& row : search_result.rows) {
    if (!contains_string(request.plan.corpus_ids, row.corpus_id)) {
      continue;
    }
    if (!contains_all_tags(row.tags, request.normalized_query.domain_tags)) {
      continue;
    }
    if (!matches_language(row.language, request.required_language)) {
      continue;
    }
    if (!matches_authority_level(row.authority_level,
                                 request.normalized_query.query_kind)) {
      continue;
    }
    filtered_rows.push_back(row);
  }

  auto hits = expand_sentence_window(filtered_rows, expression);
  std::sort(hits.begin(), hits.end(), [](const RecallHit& left, const RecallHit& right) {
    if (left.score != right.score) {
      return left.score > right.score;
    }
    if (left.updated_at != right.updated_at) {
      return left.updated_at > right.updated_at;
    }
    return left.chunk_id < right.chunk_id;
  });

  if (hits.size() > request.plan.sparse_top_k) {
    hits.resize(request.plan.sparse_top_k);
  }

  SparseRetrieveResult result;
  result.ok = true;
  result.hits = std::move(hits);
  result.warnings = std::move(search_result.warnings);
  return result;
}

SparseQueryExpression SparseRetriever::build_query_expression(
    const SparseRetrieveRequest& request) const {
  const auto lexical_terms = derive_terms(request.normalized_query);
  if (lexical_terms.empty()) {
    return {};
  }

  SparseQueryExpression expression;
  expression.lexical_terms = lexical_terms;
  expression.exact_phrase_preferred =
      request.normalized_query.prefer_exact_match && lexical_terms.size() > 1U;

  const auto term_expression = build_term_expression(lexical_terms);
  if (expression.exact_phrase_preferred) {
    expression.match_expression =
        "(" + build_phrase_expression(lexical_terms) + ") OR (" +
        term_expression + ")";
  } else {
    expression.match_expression = term_expression;
  }

  return expression;
}

std::vector<RecallHit> SparseRetriever::expand_sentence_window(
    const std::vector<SparseSearchRow>& rows,
    const SparseQueryExpression& expression) const {
  std::vector<RecallHit> hits;
  hits.reserve(rows.size());

  for (const auto& row : rows) {
    auto snippet = build_window_snippet(row.chunk_text,
                                        expression.lexical_terms,
                                        policy_);
    if (snippet.empty()) {
      snippet = collapse_whitespace(row.chunk_text);
      if (snippet.size() > policy_.max_snippet_characters &&
          policy_.max_snippet_characters > 3U) {
        snippet.resize(policy_.max_snippet_characters - 3U);
        snippet += "...";
      }
    }

    hits.push_back(RecallHit{
        .corpus_id = row.corpus_id,
        .document_id = row.document_id,
        .chunk_id = row.chunk_id,
        .score = row.score,
        .raw_snippet = std::move(snippet),
        .citation_ref = row.citation_ref,
        .updated_at = row.updated_at,
        .authority_level = row.authority_level,
        .tags = row.tags,
    });
  }

  return hits;
}

}  // namespace dasall::knowledge::retrieve