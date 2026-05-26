#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "query/QueryNormalizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::query::QueryNormalizePolicy;
using dasall::knowledge::query::QueryNormalizer;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] QueryNormalizePolicy make_policy() {
  QueryNormalizePolicy policy;
  policy.max_query_text_bytes = 40U;
  policy.max_lexical_terms = 6U;
  policy.max_top_k = 12U;
  policy.max_context_projection_items = 8U;
  policy.allowed_domain_tags = {"architecture", "runtime", "adr"};
  policy.allowed_corpora = {"knowledge-core", "runtime-ssot"};
  policy.domain_tag_aliases = {
      {"arch", "architecture"},
      {"design-doc", "adr"},
  };
  return policy;
}

void assert_warning_present(const std::vector<std::string>& warnings, const std::string& warning) {
  assert_true(std::find(warnings.begin(), warnings.end(), warning) != warnings.end(),
              "expected warning missing: " + warning);
}

void test_query_normalizer_rejects_empty_queries_with_validation_error() {
  const QueryNormalizer normalizer(make_policy());
  const KnowledgeQuery query{
      .request_id = "req-query-normalizer-empty",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "   \t  ",
      .query_kind = KnowledgeQueryKind::FactLookup,
      .domain_tags = {},
      .allowed_corpora = {},
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 8U,
      .max_context_projection_items = 6U,
      .allow_stale = false,
      .retrieval_evidence_budget_hint = 0U,
  };

  const auto result = normalizer.normalize(query);

  assert_true(result.has_consistent_values(),
              "validation failure should still produce a structured NormalizeResult");
  assert_true(!result.ok, "all-whitespace query should be rejected");
  assert_true(result.error.has_value(),
              "validation failure should return ErrorInfo");
  assert_equal(static_cast<int>(KnowledgeErrorCode::QueryValidationFailed),
               *result.error->details.code,
               "all-whitespace query should map to QueryValidationFailed");
}

void test_query_normalizer_truncates_and_filters_without_silently_widening_scope() {
  const QueryNormalizer normalizer(make_policy());
  const KnowledgeQuery query{
      .request_id = "req-query-normalizer-boundary",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "Runtime policy snapshot consistency under stale reads and refresh overlap",
      .query_kind = KnowledgeQueryKind::DiagnosticContext,
      .domain_tags = {"arch", "unknown tag", "###"},
      .allowed_corpora = {"knowledge core", "rogue corpus", "###"},
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 0U,
      .max_context_projection_items = 0U,
      .allow_stale = false,
      .retrieval_evidence_budget_hint = 0U,
  };

  const auto result = normalizer.normalize(query);

  assert_true(result.has_consistent_values(),
              "normalized boundary case should stay internally consistent");
  assert_true(result.ok, "long diagnostic query should be truncated instead of rejected");
  assert_equal(12, static_cast<int>(result.normalized_query->top_k),
               "zero top_k should default to the diagnostic query ceiling");
  assert_equal(8, static_cast<int>(result.normalized_query->max_context_projection_items),
               "zero projection items should default to the diagnostic query ceiling");
  assert_true(result.normalized_query->domain_tags ==
                  std::vector<std::string>({"architecture"}),
              "normalizer should keep only allowlisted domain tags after alias resolution");
  assert_true(result.normalized_query->allowed_corpora ==
                  std::vector<std::string>({"knowledge-core"}),
              "normalizer should keep only allowlisted corpora after canonicalization");
  assert_warning_present(result.normalized_query->warnings, "query_text_truncated");
  assert_warning_present(result.normalized_query->warnings, "top_k_defaulted");
  assert_warning_present(result.normalized_query->warnings, "max_context_projection_items_defaulted");
  assert_warning_present(result.normalized_query->warnings, "domain_tag_dropped_invalid");
  assert_warning_present(result.normalized_query->warnings, "domain_tag_filtered_allowlist");
  assert_warning_present(result.normalized_query->warnings, "allowed_corpus_dropped_invalid");
  assert_warning_present(result.normalized_query->warnings, "allowed_corpus_filtered_allowlist");
}

void test_query_normalizer_rejects_reserved_multihop_queries_as_not_supported() {
  const QueryNormalizer normalizer(make_policy());
  const KnowledgeQuery query{
      .request_id = "req-query-normalizer-multihop",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "Correlate stale context with recovery history",
      .query_kind = KnowledgeQueryKind::MultiHop,
      .domain_tags = {},
      .allowed_corpora = {},
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 8U,
      .max_context_projection_items = 6U,
      .allow_stale = false,
      .retrieval_evidence_budget_hint = 0U,
  };

  const auto result = normalizer.normalize(query);

  assert_true(result.has_consistent_values(),
              "not-supported path should still return a structured NormalizeResult");
  assert_true(!result.ok, "reserved multihop query should be rejected in v1");
  assert_equal(static_cast<int>(KnowledgeErrorCode::NotSupported),
               *result.error->details.code,
               "reserved multihop query should map to NotSupported");
  assert_equal(std::string("not_supported"), result.error->source_ref.ref_id,
               "reserved multihop query should expose a stable not_supported ref id");
}

void test_query_normalizer_rejects_invalid_required_filters() {
  const QueryNormalizer normalizer(make_policy());
  const KnowledgeQuery query{
      .request_id = "req-query-normalizer-required-filter-invalid",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "Inspect runtime hybrid rollout readiness",
      .query_kind = KnowledgeQueryKind::DiagnosticContext,
      .domain_tags = {"runtime"},
      .allowed_corpora = {"knowledge-core"},
      .required_tags = {"###"},
      .required_language = std::string("   "),
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 8U,
      .max_context_projection_items = 6U,
      .allow_stale = false,
      .retrieval_evidence_budget_hint = 0U,
  };

  const auto result = normalizer.normalize(query);

  assert_true(result.has_consistent_values(),
              "invalid required filters should still return a structured NormalizeResult");
  assert_true(!result.ok,
              "invalid required_tags or required_language should be rejected");
  assert_equal(static_cast<int>(KnowledgeErrorCode::QueryValidationFailed),
               *result.error->details.code,
               "invalid required filters should map to QueryValidationFailed");
  assert_equal(std::string("query_validation_failed"), result.error->source_ref.ref_id,
               "invalid required filters should expose the stable validation ref id");
}

}  // namespace

int main() {
  try {
    test_query_normalizer_rejects_empty_queries_with_validation_error();
    test_query_normalizer_truncates_and_filters_without_silently_widening_scope();
    test_query_normalizer_rejects_reserved_multihop_queries_as_not_supported();
    test_query_normalizer_rejects_invalid_required_filters();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}