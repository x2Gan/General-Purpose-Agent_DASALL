#include <exception>
#include <iostream>
#include <string>

#include "KnowledgeEvidenceProjector.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::RetrievalEvidenceRef make_ref(
    std::string evidence_ref,
    std::string source_ref,
    std::string freshness) {
  return dasall::contracts::RetrievalEvidenceRef{
      .evidence_ref = std::move(evidence_ref),
      .source_ref = std::move(source_ref),
      .source_kind = "knowledge_chunk",
      .summary_text = "projected knowledge evidence should preserve summary text",
      .trust_level = "trusted",
      .freshness = std::move(freshness),
      .anchor_locator = std::string{"section-1"},
  };
}

void test_knowledge_evidence_projector_appends_unique_text_and_refs() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::memory::MemoryContextRequest request;
  request.external_evidence = {"runtime:baseline-evidence"};
  request.retrieval_evidence_refs = {make_ref("existing-ref", "docs#existing", "fresh")};

  dasall::knowledge::KnowledgeRetrieveResult result;
  result.ok = true;
  result.evidence = dasall::knowledge::EvidenceBundle{
      .slices = {dasall::knowledge::EvidenceSlice{
                     .evidence_id = "projector-slice-001",
                     .snippet = "first projected snippet",
                     .citation_ref = "docs#slice-001",
                     .confidence = 0.91F,
                     .freshness = dasall::knowledge::FreshnessState::Fresh,
                     .tags = {"normative"},
                 }},
      .context_projection = {"knowledge projection one",
                             "knowledge projection one",
                             std::string{},
                             "knowledge projection two"},
      .omitted_sources = {},
      .degraded = false,
      .evidence_insufficient = false,
      .coverage_notes = "unit coverage",
  };
  result.retrieval_evidence_refs = {
      make_ref("existing-ref", "docs#existing", "fresh"),
      dasall::contracts::RetrievalEvidenceRef{},
      make_ref("new-ref", "docs#new", "stale"),
  };

  const dasall::runtime::KnowledgeEvidenceProjector projector;
  projector.project(result, request);

  assert_equal(3,
               static_cast<int>(request.external_evidence.size()),
               "knowledge evidence projector should preserve baseline evidence and append unique textual projections");
  assert_equal(std::string{"runtime:baseline-evidence"},
               request.external_evidence.front(),
               "knowledge evidence projector should keep preexisting runtime evidence first");
  assert_equal(std::string{"knowledge projection one"},
               request.external_evidence.at(1),
               "knowledge evidence projector should append the first knowledge text projection once");
  assert_equal(std::string{"knowledge projection two"},
               request.external_evidence.at(2),
               "knowledge evidence projector should append the second distinct knowledge text projection");
  assert_equal(2,
               static_cast<int>(request.retrieval_evidence_refs.size()),
               "knowledge evidence projector should ignore duplicate or invalid structured refs");
  assert_equal(std::string{"new-ref"},
               request.retrieval_evidence_refs.back().evidence_ref,
               "knowledge evidence projector should append the new structured evidence ref");
  assert_equal(std::string{"stale"},
               request.retrieval_evidence_refs.back().freshness,
               "knowledge evidence projector should preserve structured freshness when projecting refs");
  assert_true(request.retrieval_evidence_refs.front().has_consistent_values(),
              "knowledge evidence projector should keep previously valid structured refs unchanged");
}

void test_knowledge_evidence_projector_ignores_failed_or_unstructured_results() {
  using dasall::tests::support::assert_equal;

  dasall::memory::MemoryContextRequest request;
  request.external_evidence = {"runtime:baseline-evidence"};
  request.retrieval_evidence_refs = {make_ref("existing-ref", "docs#existing", "fresh")};

  dasall::knowledge::KnowledgeRetrieveResult failed_result;
  failed_result.ok = false;
  failed_result.evidence = dasall::knowledge::EvidenceBundle{
      .slices = {},
      .context_projection = {"should not project failed knowledge result"},
      .omitted_sources = {},
      .degraded = true,
      .evidence_insufficient = true,
      .coverage_notes = "failed result",
  };
  failed_result.retrieval_evidence_refs = {make_ref("failed-ref", "docs#failed", "unknown")};

  dasall::knowledge::KnowledgeRetrieveResult missing_evidence_result;
  missing_evidence_result.ok = true;
  missing_evidence_result.retrieval_evidence_refs = {
      make_ref("missing-evidence-ref", "docs#missing", "fresh")};

  const dasall::runtime::KnowledgeEvidenceProjector projector;
  projector.project(failed_result, request);
  projector.project(missing_evidence_result, request);

  assert_equal(1,
               static_cast<int>(request.external_evidence.size()),
               "knowledge evidence projector should not append text when the knowledge result is failed or lacks structured evidence");
  assert_equal(1,
               static_cast<int>(request.retrieval_evidence_refs.size()),
               "knowledge evidence projector should not append refs when the knowledge result is failed or lacks structured evidence");
}

}  // namespace

int main() {
  try {
    test_knowledge_evidence_projector_appends_unique_text_and_refs();
    test_knowledge_evidence_projector_ignores_failed_or_unstructured_results();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}