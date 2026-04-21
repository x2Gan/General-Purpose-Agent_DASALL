#include "query/QueryNormalizer.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace dasall::knowledge::query {

namespace {

struct QueryKindDefaults {
  std::size_t max_top_k = 8U;
  std::size_t max_context_projection_items = 6U;
  bool prefer_exact_match = false;
};

[[nodiscard]] bool is_ascii_whitespace(unsigned char value) {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' ||
         value == '\v';
}

[[nodiscard]] bool is_ascii_alnum(unsigned char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z');
}

[[nodiscard]] char to_lower_ascii(unsigned char value) {
  if (value >= 'A' && value <= 'Z') {
    return static_cast<char>(value - 'A' + 'a');
  }

  return static_cast<char>(value);
}

void append_unique(std::vector<std::string>& values, std::string value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

[[nodiscard]] std::size_t utf8_safe_prefix_length(std::string_view text, std::size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return text.size();
  }

  std::size_t safe_length = max_bytes;
  while (safe_length > 0U &&
         (static_cast<unsigned char>(text[safe_length]) & 0xC0U) == 0x80U) {
    --safe_length;
  }

  return safe_length;
}

[[nodiscard]] std::string trim_spaces(std::string value) {
  while (!value.empty() && value.back() == ' ') {
    value.pop_back();
  }

  return value;
}

[[nodiscard]] std::string normalize_identifier(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());

  bool previous_was_separator = false;
  for (const unsigned char byte : value) {
    if (is_ascii_alnum(byte)) {
      normalized.push_back(to_lower_ascii(byte));
      previous_was_separator = false;
      continue;
    }

    if (byte == '-' || byte == '_') {
      if (!normalized.empty() && !previous_was_separator) {
        normalized.push_back(static_cast<char>(byte));
        previous_was_separator = true;
      }
      continue;
    }

    if (is_ascii_whitespace(byte) || std::ispunct(byte)) {
      if (!normalized.empty() && !previous_was_separator) {
        normalized.push_back('-');
        previous_was_separator = true;
      }
      continue;
    }

    normalized.push_back(static_cast<char>(byte));
    previous_was_separator = false;
  }

  while (!normalized.empty() && (normalized.back() == '-' || normalized.back() == '_')) {
    normalized.pop_back();
  }

  return normalized;
}

[[nodiscard]] bool contains_value(const std::vector<std::string>& values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] std::vector<std::string> canonicalize_values(
    const std::vector<std::string>& values,
    bool* saw_invalid = nullptr) {
  std::vector<std::string> canonical_values;
  for (const auto& value : values) {
    auto canonical = normalize_identifier(value);
    if (canonical.empty()) {
      if (saw_invalid != nullptr) {
        *saw_invalid = true;
      }
      continue;
    }

    append_unique(canonical_values, std::move(canonical));
  }

  return canonical_values;
}

[[nodiscard]] std::optional<std::string> resolve_tag_alias(
    const std::map<std::string, std::string>& aliases,
    std::string_view value) {
  for (const auto& [alias, canonical] : aliases) {
    if (normalize_identifier(alias) == value) {
      return normalize_identifier(canonical);
    }
  }

  return std::nullopt;
}

[[nodiscard]] QueryKindDefaults defaults_for_query_kind(KnowledgeQueryKind query_kind) {
  switch (query_kind) {
    case KnowledgeQueryKind::FactLookup:
      return QueryKindDefaults{.max_top_k = 8U, .max_context_projection_items = 6U, .prefer_exact_match = true};
    case KnowledgeQueryKind::ProcedureLookup:
      return QueryKindDefaults{.max_top_k = 10U, .max_context_projection_items = 6U, .prefer_exact_match = false};
    case KnowledgeQueryKind::DiagnosticContext:
      return QueryKindDefaults{.max_top_k = 12U, .max_context_projection_items = 8U, .prefer_exact_match = false};
    case KnowledgeQueryKind::PolicyEvidence:
      return QueryKindDefaults{.max_top_k = 8U, .max_context_projection_items = 6U, .prefer_exact_match = true};
    case KnowledgeQueryKind::MultiHop:
      break;
  }

  return QueryKindDefaults{};
}

[[nodiscard]] NormalizeResult make_error_result(KnowledgeErrorCode code,
                                                std::string message,
                                                std::string ref_id) {
  NormalizeResult result;
  result.ok = false;
  result.error = make_knowledge_error_info(code,
                                           std::move(message),
                                           "query_normalizer.normalize",
                                           std::move(ref_id));
  return result;
}

}  // namespace

bool NormalizedQuery::has_consistent_values() const {
  return !request_id.empty() && !normalized_text.empty() && !lexical_terms.empty() &&
         query_kind != KnowledgeQueryKind::MultiHop && top_k > 0U &&
         max_context_projection_items > 0U && detail::has_unique_values(lexical_terms) &&
         detail::has_unique_values(domain_tags) && detail::has_unique_values(allowed_corpora) &&
         detail::has_unique_values(warnings);
}

bool NormalizeResult::has_consistent_values() const {
  if (ok) {
    return normalized_query.has_value() && normalized_query->has_consistent_values() &&
           !error.has_value();
  }

  return !normalized_query.has_value() && error.has_value() && detail::has_error_shape(error);
}

bool QueryNormalizePolicy::has_consistent_values() const {
  if (max_query_text_bytes == 0U || max_lexical_terms == 0U || max_top_k == 0U ||
      max_context_projection_items == 0U) {
    return false;
  }

  bool saw_invalid_domain_tag = false;
  const auto canonical_domain_tags =
      canonicalize_values(allowed_domain_tags, &saw_invalid_domain_tag);
  if (saw_invalid_domain_tag || canonical_domain_tags.size() != allowed_domain_tags.size()) {
    return false;
  }

  bool saw_invalid_corpus = false;
  const auto canonical_corpora = canonicalize_values(allowed_corpora, &saw_invalid_corpus);
  if (saw_invalid_corpus || canonical_corpora.size() != allowed_corpora.size()) {
    return false;
  }

  for (const auto& [alias, canonical] : domain_tag_aliases) {
    const auto canonical_alias = normalize_identifier(alias);
    const auto canonical_tag = normalize_identifier(canonical);
    if (canonical_alias.empty() || canonical_tag.empty()) {
      return false;
    }
    if (!canonical_domain_tags.empty() && !contains_value(canonical_domain_tags, canonical_tag)) {
      return false;
    }
  }

  return true;
}

QueryNormalizer::QueryNormalizer(QueryNormalizePolicy policy) : policy_(std::move(policy)) {}

NormalizeResult QueryNormalizer::normalize(const KnowledgeQuery& query) const {
  if (!policy_.has_consistent_values()) {
    return make_error_result(KnowledgeErrorCode::InternalError,
                             "query normalize policy is inconsistent",
                             "normalizer_policy_inconsistent");
  }

  if (query.request_id.empty()) {
    return make_error_result(KnowledgeErrorCode::QueryValidationFailed,
                             "request_id must be non-empty",
                             "query_validation_failed");
  }

  if (query.query_kind == KnowledgeQueryKind::MultiHop) {
    return make_error_result(KnowledgeErrorCode::NotSupported,
                             "multi-hop query kind is reserved in v1",
                             "not_supported");
  }

  std::vector<std::string> warnings;
  const auto normalized_text = canonicalize_text(query.query_text, warnings);
  if (normalized_text.empty()) {
    return make_error_result(KnowledgeErrorCode::QueryValidationFailed,
                             "query_text must contain non-whitespace content",
                             "query_validation_failed");
  }

  const auto lexical_terms = derive_lexical_terms(normalized_text, warnings);
  if (lexical_terms.empty()) {
    return make_error_result(KnowledgeErrorCode::QueryValidationFailed,
                             "query_text produced no lexical terms after normalization",
                             "query_validation_failed");
  }

  const auto defaults = defaults_for_query_kind(query.query_kind);
  const auto domain_tags = normalize_tags(query.domain_tags, warnings);
  const auto allowed_corpora = normalize_allowed_corpora(query.allowed_corpora, warnings);

  std::size_t top_k = query.top_k == 0U ? defaults.max_top_k : query.top_k;
  if (query.top_k == 0U) {
    append_unique(warnings, "top_k_defaulted");
  }
  const auto top_k_ceiling = std::min(policy_.max_top_k, defaults.max_top_k);
  if (top_k > top_k_ceiling) {
    top_k = top_k_ceiling;
    append_unique(warnings, "top_k_clamped");
  }

  std::size_t max_context_projection_items =
      query.max_context_projection_items == 0U ? defaults.max_context_projection_items
                                               : query.max_context_projection_items;
  if (query.max_context_projection_items == 0U) {
    append_unique(warnings, "max_context_projection_items_defaulted");
  }
  const auto projection_ceiling =
      std::min(policy_.max_context_projection_items, defaults.max_context_projection_items);
  if (max_context_projection_items > projection_ceiling) {
    max_context_projection_items = projection_ceiling;
    append_unique(warnings, "max_context_projection_items_clamped");
  }

  NormalizeResult result;
  result.ok = true;
  result.normalized_query = NormalizedQuery{
      .request_id = query.request_id,
      .normalized_text = normalized_text,
      .lexical_terms = lexical_terms,
      .domain_tags = domain_tags,
      .allowed_corpora = allowed_corpora,
      .query_kind = query.query_kind,
      .top_k = top_k,
      .max_context_projection_items = max_context_projection_items,
      .prefer_exact_match = defaults.prefer_exact_match,
      .allow_stale = query.allow_stale,
      .warnings = warnings,
  };
  return result;
}

std::string QueryNormalizer::canonicalize_text(std::string_view query_text,
                                               std::vector<std::string>& warnings) const {
  std::string canonical_text;
  canonical_text.reserve(query_text.size());

  bool previous_was_space = true;
  for (const unsigned char byte : query_text) {
    if (is_ascii_whitespace(byte)) {
      if (!canonical_text.empty() && !previous_was_space) {
        canonical_text.push_back(' ');
      }
      previous_was_space = true;
      continue;
    }

    canonical_text.push_back(byte < 128U ? to_lower_ascii(byte) : static_cast<char>(byte));
    previous_was_space = false;
  }

  canonical_text = trim_spaces(std::move(canonical_text));
  if (canonical_text.size() > policy_.max_query_text_bytes) {
    canonical_text.resize(utf8_safe_prefix_length(canonical_text, policy_.max_query_text_bytes));
    canonical_text = trim_spaces(std::move(canonical_text));
    append_unique(warnings, "query_text_truncated");
  }

  return canonical_text;
}

std::vector<std::string> QueryNormalizer::derive_lexical_terms(
    std::string_view text,
    std::vector<std::string>& warnings) const {
  std::vector<std::string> lexical_terms;
  std::string current_term;

  auto flush_current_term = [&]() {
    if (current_term.empty()) {
      return;
    }

    if (!contains_value(lexical_terms, current_term)) {
      if (lexical_terms.size() < policy_.max_lexical_terms) {
        lexical_terms.push_back(current_term);
      } else {
        append_unique(warnings, "lexical_terms_clamped");
      }
    }
    current_term.clear();
  };

  for (const unsigned char byte : text) {
    if (is_ascii_alnum(byte) || byte >= 128U) {
      current_term.push_back(byte < 128U ? to_lower_ascii(byte) : static_cast<char>(byte));
      continue;
    }

    flush_current_term();
  }

  flush_current_term();
  return lexical_terms;
}

std::vector<std::string> QueryNormalizer::normalize_tags(const std::vector<std::string>& tags,
                                                         std::vector<std::string>& warnings) const {
  bool saw_invalid = false;
  auto normalized_tags = canonicalize_values(tags, &saw_invalid);
  if (saw_invalid) {
    append_unique(warnings, "domain_tag_dropped_invalid");
  }

  std::vector<std::string> filtered_tags;
  const auto allowed_tags = canonicalize_values(policy_.allowed_domain_tags);
  for (auto& tag : normalized_tags) {
    if (const auto alias = resolve_tag_alias(policy_.domain_tag_aliases, tag); alias.has_value()) {
      tag = *alias;
    }

    if (!allowed_tags.empty() && !contains_value(allowed_tags, tag)) {
      append_unique(warnings, "domain_tag_filtered_allowlist");
      continue;
    }

    append_unique(filtered_tags, std::move(tag));
  }

  return filtered_tags;
}

std::vector<std::string> QueryNormalizer::normalize_allowed_corpora(
    const std::vector<std::string>& corpora,
    std::vector<std::string>& warnings) const {
  bool saw_invalid = false;
  const auto normalized_corpora = canonicalize_values(corpora, &saw_invalid);
  if (saw_invalid) {
    append_unique(warnings, "allowed_corpus_dropped_invalid");
  }

  std::vector<std::string> filtered_corpora;
  const auto allowed_corpora = canonicalize_values(policy_.allowed_corpora);
  for (const auto& corpus : normalized_corpora) {
    if (!allowed_corpora.empty() && !contains_value(allowed_corpora, corpus)) {
      append_unique(warnings, "allowed_corpus_filtered_allowlist");
      continue;
    }

    append_unique(filtered_corpora, corpus);
  }

  return filtered_corpora;
}

}  // namespace dasall::knowledge::query