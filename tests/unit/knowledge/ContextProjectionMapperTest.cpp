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

[[nodiscard]] RankedHit make_ranked_hit() {
  RecallHit hit;
  hit.corpus_id = "ssot_normative";
  hit.document_id = "CrossModuleDataProjectionMatrix";
  hit.chunk_id = "chunk-projection-1";
  hit.score = 0.8F;
  hit.raw_snippet = "  ContextPacket\nretrieval_evidence\tkeeps only projected strings.  ";
  hit.citation_ref = "docs/ssot/CrossModuleDataProjectionMatrix.md#L27";
  hit.updated_at = 7;
  hit.authority_level = AuthorityLevel::Reference;
  hit.tags = {"ssot", "projection"};

  return RankedHit{
      .hit = std::move(hit),
      .fused_score = 0.9F,
      .stale = true,
      .score_reason_codes = {"rrf_sparse_only", "stale_penalty_applied"},
  };
}

void test_context_projection_mapper_collapses_whitespace_and_marks_stale_entries() {
  const EvidenceAssembler assembler;
  const RankedHitSet ranked_hits{
      .hits = {make_ranked_hit()},
      .degraded = false,
  };

  const auto bundle = assembler.assemble(ranked_hits,
                                         EvidenceAssemblePolicy{
                                             .evidence_budget_tokens = 128U,
                                             .max_context_projection_items = 2U,
                                         });

  assert_equal(1, static_cast<int>(bundle.context_projection.size()),
               "single ranked hit should project to exactly one line");
  assert_equal("[reference][stale] ContextPacket retrieval_evidence keeps only projected strings. (docs/ssot/CrossModuleDataProjectionMatrix.md#L27)",
               bundle.context_projection.front(),
               "projection mapper should collapse whitespace and expose stale state inline");
  assert_true(bundle.context_projection.front().find('\n') == std::string::npos,
              "projection lines must stay single-line for ContextPacket handoff");
}

}  // namespace

int main() {
  try {
    test_context_projection_mapper_collapses_whitespace_and_marks_stale_entries();
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "ContextProjectionMapperTest failed: " << ex.what() << std::endl;
  }

  return 1;
}