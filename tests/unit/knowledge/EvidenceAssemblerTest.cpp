#include <cmath>
#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "evidence/EvidenceAssembler.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::evidence::EvidenceAssemblePolicy;
using dasall::knowledge::evidence::EvidenceAssembler;
using dasall::knowledge::rerank::RankedHit;
using dasall::knowledge::rerank::RankedHitSet;
using dasall::knowledge::retrieve::RecallHit;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RecallHit make_hit(std::string chunk_id,
                                 std::string snippet,
                                 std::string citation_ref) {
  RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "ADR-007";
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.9F;
  hit.raw_snippet = std::move(snippet);
  hit.citation_ref = std::move(citation_ref);
  hit.updated_at = 42;
  hit.authority_level = AuthorityLevel::Normative;
  hit.tags = {"adr", "runtime"};
  return hit;
}

void assert_float_near(float expected, float actual, const std::string& message) {
  assert_true(std::fabs(expected - actual) < 0.0001F,
              message + ": expected=" + std::to_string(expected) +
                  " actual=" + std::to_string(actual));
}

void test_evidence_assembler_builds_structured_slices_and_projection_in_rank_order() {
  const EvidenceAssembler assembler;
  const RankedHitSet ranked_hits{
      .hits = {
          RankedHit{
              .hit = make_hit("chunk-1",
                              "RecoveryManager owns recovery admission.",
                              "docs/adr/ADR-007#section-2"),
              .fused_score = 1.0F,
              .stale = false,
              .score_reason_codes = {"rrf_sparse_only"},
          },
          RankedHit{
              .hit = make_hit("chunk-2",
                              "Stale snapshot results are still traceable.",
                              "docs/ssot/CrossModuleDataProjectionMatrix.md#knowledge"),
              .fused_score = 0.8F,
              .stale = true,
              .score_reason_codes = {"rrf_dense_only", "stale_penalty_applied"},
          },
      },
      .degraded = false,
  };

  const auto bundle = assembler.assemble(ranked_hits,
                                         EvidenceAssemblePolicy{
                                             .evidence_budget_tokens = 128U,
                                             .max_context_projection_items = 4U,
                                         });

  assert_true(bundle.has_consistent_values(),
              "assembled evidence bundle should satisfy public invariants");
  assert_equal(2, static_cast<int>(bundle.slices.size()),
               "assembler should keep every valid ranked hit as a slice");
  assert_equal(2, static_cast<int>(bundle.context_projection.size()),
               "assembler should map every slice into projection when budget allows");
  assert_equal("adr_normative/ADR-007/chunk-1",
               bundle.slices.front().evidence_id,
               "evidence id should stay deterministic across bundle assembly");
  assert_true(bundle.slices.front().freshness == dasall::knowledge::FreshnessState::Fresh,
              "fresh hits should stay marked as fresh");
  assert_true(bundle.slices.back().freshness ==
                  dasall::knowledge::FreshnessState::StaleAllowed,
              "stale hits should stay explicitly marked on the slice");
  assert_float_near(1.0F,
                    bundle.slices.front().confidence,
                    "highest fused score should normalize to confidence 1.0");
  assert_float_near(0.72F,
                    bundle.slices.back().confidence,
                    "stale confidence should apply the 10 percent assembly penalty");
  assert_true(!bundle.degraded,
              "valid ranked hits without omissions should not degrade the bundle");
  assert_true(!bundle.evidence_insufficient,
              "non-empty projection should clear evidence_insufficient");
}

void test_assemble_policy_prefers_runtime_budget_hint_and_caps_projection_items() {
  KnowledgeQuery query{
      .request_id = "req-011-policy",
      .session_id = std::nullopt,
      .goal_id = std::nullopt,
      .query_text = "show me recovery admission evidence",
      .query_kind = dasall::knowledge::KnowledgeQueryKind::FactLookup,
      .domain_tags = {},
      .allowed_corpora = {},
      .latest_observation_digest_summary = std::nullopt,
      .belief_state_summary = std::nullopt,
      .top_k = 8U,
      .max_context_projection_items = 8U,
      .allow_stale = false,
      .retrieval_evidence_budget_hint = 96U,
  };
  KnowledgeConfigSnapshot config{
      .knowledge_enabled = true,
      .vector_enabled = false,
      .retrieval_mode_default = RetrievalMode::LexicalOnly,
      .evidence_budget_tokens = 256U,
      .max_context_projection_items = 6U,
      .catalog_refresh_interval_ms = 30000,
      .catalog_expire_after_ms = 60000,
      .allow_stale_read = true,
      .failure_backoff_ms = 1000,
      .request_deadline_ms = 900,
      .allow_budget_degrade = true,
      .max_parallel_recall = 1U,
      .sparse_recall_timeout_ms = 200,
      .dense_recall_timeout_ms = 200,
      .ingest_timeout_ms = 1000,
  };

  const auto policy = EvidenceAssemblePolicy::from_query(query, config);
  assert_true(policy.has_consistent_values(),
              "derived evidence assemble policy should be internally consistent");
  assert_equal(96,
               static_cast<int>(policy.evidence_budget_tokens),
               "runtime evidence budget hint should override config default");
  assert_equal(6,
               static_cast<int>(policy.max_context_projection_items),
               "config max projection items should cap the query request");
}

}  // namespace

int main() {
  try {
    test_evidence_assembler_builds_structured_slices_and_projection_in_rank_order();
    test_assemble_policy_prefers_runtime_budget_hint_and_caps_projection_items();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "EvidenceAssemblerTest failed: " << ex.what() << std::endl;
  }

  return 1;
}