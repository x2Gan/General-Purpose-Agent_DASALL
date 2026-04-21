#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "evidence/EvidenceAssembler.h"

namespace {

using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::evidence::EvidenceAssemblePolicy;
using dasall::knowledge::evidence::EvidenceAssembler;
using dasall::knowledge::rerank::RankedHit;
using dasall::knowledge::rerank::RankedHitSet;
using dasall::knowledge::retrieve::RecallHit;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] RankedHit make_ranked_hit(std::string chunk_id,
                                        std::string snippet,
                                        std::string citation_ref) {
  RecallHit hit;
  hit.corpus_id = "docs_normative";
  hit.document_id = "projection-budget";
  hit.chunk_id = std::move(chunk_id);
  hit.score = 0.8F;
  hit.raw_snippet = std::move(snippet);
  hit.citation_ref = std::move(citation_ref);
  hit.updated_at = 10;
  hit.authority_level = AuthorityLevel::Reference;
  hit.tags = {"budget"};

  return RankedHit{
      .hit = std::move(hit),
      .fused_score = 0.7F,
      .stale = false,
      .score_reason_codes = {"rrf_sparse_only"},
  };
}

void test_budget_clamp_truncates_projection_before_dropping_structured_slices() {
  const EvidenceAssembler assembler;
  const RankedHitSet ranked_hits{
      .hits = {
          make_ranked_hit("chunk-1",
              "Projection stays traceable.",
              "guide#chunk-1"),
          make_ranked_hit("chunk-2",
              "Structured slices survive budget truncation.",
              "guide#chunk-2"),
      },
      .degraded = false,
  };

  const auto bundle = assembler.assemble(ranked_hits,
                                         EvidenceAssemblePolicy{
                       .evidence_budget_tokens = 24U,
                                             .max_context_projection_items = 4U,
                                         });

  assert_equal(2, static_cast<int>(bundle.slices.size()),
               "budget clamp should not delete structured slices");
  assert_equal(1, static_cast<int>(bundle.context_projection.size()),
               "budget clamp should truncate projection lines first");
  assert_equal(1, static_cast<int>(bundle.omitted_sources.size()),
               "truncated projection should record omitted sources");
  assert_equal("guide#chunk-2",
               bundle.omitted_sources.front(),
               "omitted_sources should keep the first source dropped by budget");
  assert_true(bundle.degraded,
              "projection truncation should mark the evidence bundle degraded");
  assert_true(!bundle.evidence_insufficient,
              "remaining projection entries should still count as sufficient evidence");
}

void test_budget_exhaustion_marks_evidence_insufficient_when_no_projection_fits() {
  const EvidenceAssembler assembler;
  const RankedHitSet ranked_hits{
      .hits = {make_ranked_hit("chunk-1",
                               "This snippet is intentionally longer than the tiny budget.",
                               "docs/guide#budget-exhausted")},
      .degraded = false,
  };

  const auto bundle = assembler.assemble(ranked_hits,
                                         EvidenceAssemblePolicy{
                                             .evidence_budget_tokens = 4U,
                                             .max_context_projection_items = 2U,
                                         });

  assert_equal(1, static_cast<int>(bundle.slices.size()),
               "budget exhaustion should still keep the structured slice for diagnostics");
  assert_equal(0, static_cast<int>(bundle.context_projection.size()),
               "tiny budgets should yield an empty projection when nothing fits safely");
  assert_true(bundle.evidence_insufficient,
              "empty projection should surface evidence_insufficient to runtime");
  assert_true(bundle.degraded,
              "budget exhaustion should mark the bundle degraded");
  assert_equal("docs/guide#budget-exhausted",
               bundle.omitted_sources.front(),
               "budget exhaustion should record the omitted source for traceability");
}

}  // namespace

int main() {
  try {
    test_budget_clamp_truncates_projection_before_dropping_structured_slices();
    test_budget_exhaustion_marks_evidence_insufficient_when_no_projection_fits();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "EvidenceBudgetClampTest failed: " << ex.what() << std::endl;
  }

  return 1;
}