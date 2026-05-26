#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "query/QueryNormalizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::query::NormalizeResult;
using dasall::knowledge::query::QueryNormalizePolicy;
using dasall::knowledge::query::QueryNormalizer;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] QueryNormalizePolicy make_policy() {
  QueryNormalizePolicy policy;
  policy.max_query_text_bytes = 256U;
  policy.max_lexical_terms = 12U;
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

void assert_warning_absent(const NormalizeResult& result, const std::string& warning) {
  assert_true(std::find(result.normalized_query->warnings.begin(),
                        result.normalized_query->warnings.end(),
                        warning) == result.normalized_query->warnings.end(),
              "unexpected warning present: " + warning);
}

void assert_warning_present(const NormalizeResult& result, const std::string& warning) {
  assert_true(std::find(result.normalized_query->warnings.begin(),
                        result.normalized_query->warnings.end(),
                        warning) != result.normalized_query->warnings.end(),
              "expected warning missing: " + warning);
}

void test_query_normalizer_canonicalizes_text_aliases_and_filters_deterministically() {
  const QueryNormalizer normalizer(make_policy());
  const KnowledgeQuery query{
      .request_id = "req-query-normalizer-01",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "  SQLITE-FTS5\tSnapshot   swap??  ",
      .preferred_mode = dasall::knowledge::RetrievalMode::Hybrid,
      .query_kind = KnowledgeQueryKind::FactLookup,
      .domain_tags = {"Arch", "arch", "runtime"},
      .allowed_corpora = {"Knowledge Core", "runtime ssot", "knowledge-core"},
      .required_tags = {"Runtime Owner", "runtime-owner", "ADR"},
      .required_language = std::string("ZH-CN"),
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 8U,
      .max_context_projection_items = 6U,
      .allow_stale = true,
      .retrieval_evidence_budget_hint = 0U,
  };

  const auto result = normalizer.normalize(query);

  assert_true(result.has_consistent_values(),
              "successful normalization should produce a consistent NormalizeResult");
  assert_true(result.ok, "fact lookup query should normalize successfully");
  assert_true(result.normalized_query.has_value(),
              "successful normalization should return a NormalizedQuery");
  assert_equal(std::string("sqlite-fts5 snapshot swap??"),
               result.normalized_query->normalized_text,
               "normalizer should lowercase ascii text and collapse whitespace without rewriting semantics");
  assert_true(result.normalized_query->lexical_terms ==
                  std::vector<std::string>({"sqlite", "fts5", "snapshot", "swap"}),
              "normalizer should derive deterministic lexical terms from punctuation-separated text");
  assert_true(result.normalized_query->domain_tags ==
                  std::vector<std::string>({"architecture", "runtime"}),
              "normalizer should apply alias mapping and deduplicate domain tags");
  assert_true(result.normalized_query->allowed_corpora ==
                  std::vector<std::string>({"knowledge-core", "runtime-ssot"}),
              "normalizer should canonicalize and deduplicate allowed corpora without widening the scope");
    assert_true(result.normalized_query->preferred_mode.has_value() &&
            *result.normalized_query->preferred_mode ==
              dasall::knowledge::RetrievalMode::Hybrid,
          "normalizer should preserve request-scoped preferred_mode for later runtime gating");
    assert_true(result.normalized_query->required_tags ==
            std::vector<std::string>({"runtime-owner", "adr"}),
          "normalizer should canonicalize and deduplicate required_tags independently from domain_tags");
    assert_true(result.normalized_query->required_language.has_value() &&
            *result.normalized_query->required_language == "zh-cn",
          "normalizer should canonicalize required_language to a stable lower-case identifier");
  assert_true(result.normalized_query->prefer_exact_match,
              "fact lookup queries should default to prefer_exact_match");
  assert_true(result.normalized_query->allow_stale,
              "allow_stale should flow through the normalized query unchanged");
  assert_equal(8, static_cast<int>(result.normalized_query->top_k),
               "fact lookup default top_k should preserve the ABI default when already within range");
  assert_equal(6, static_cast<int>(result.normalized_query->max_context_projection_items),
               "fact lookup default projection size should preserve the ABI default when already within range");
  assert_warning_absent(result, "top_k_clamped");
  assert_warning_absent(result, "max_context_projection_items_clamped");
}

void test_query_normalizer_clamps_procedure_queries_to_policy_and_kind_specific_limits() {
  const QueryNormalizer normalizer(make_policy());
  const KnowledgeQuery query{
      .request_id = "req-query-normalizer-02",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "How do I rotate logs safely across daemons?",
      .query_kind = KnowledgeQueryKind::ProcedureLookup,
      .domain_tags = {"runtime"},
      .allowed_corpora = {},
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 32U,
      .max_context_projection_items = 12U,
      .allow_stale = false,
      .retrieval_evidence_budget_hint = 0U,
  };

  const auto result = normalizer.normalize(query);

  assert_true(result.has_consistent_values(),
              "clamped procedure lookup should still produce a consistent NormalizeResult");
  assert_true(result.ok, "procedure lookup should normalize successfully");
  assert_true(!result.normalized_query->prefer_exact_match,
              "procedure lookup should not be forced into exact-match mode");
  assert_equal(10, static_cast<int>(result.normalized_query->top_k),
               "procedure lookup top_k should clamp to the stricter query-kind ceiling");
  assert_equal(6, static_cast<int>(result.normalized_query->max_context_projection_items),
               "procedure lookup projection items should clamp to the stricter query-kind ceiling");
  assert_warning_present(result, "top_k_clamped");
  assert_warning_present(result, "max_context_projection_items_clamped");
}

}  // namespace

int main() {
  try {
    test_query_normalizer_canonicalizes_text_aliases_and_filters_deterministically();
    test_query_normalizer_clamps_procedure_queries_to_policy_and_kind_specific_limits();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}