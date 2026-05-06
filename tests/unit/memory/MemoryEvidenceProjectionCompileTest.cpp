#include <exception>
#include <iostream>
#include <type_traits>
#include <vector>

#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"
#include "context/RetrievalEvidenceRef.h"
#include "support/TestAssertions.h"

namespace {

void test_memory_context_request_exposes_parallel_evidence_projection_surface() {
  using dasall::memory::ContextAssemblyResult;
  using dasall::memory::MemoryContextRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MemoryContextRequest{}.retrieval_evidence_refs),
                               std::vector<dasall::contracts::RetrievalEvidenceRef>>,
                "MemoryContextRequest should expose RetrievalEvidenceRef projections alongside external_evidence");

  const dasall::contracts::RetrievalEvidenceRef evidence_ref{
      .evidence_ref = "evidence-009-compile",
      .source_ref = "doc-009-compile",
      .source_kind = "knowledge_chunk",
      .summary_text = "compile surface should keep structured evidence refs available.",
      .trust_level = "medium",
      .freshness = "fresh",
      .anchor_locator = std::nullopt,
  };

  MemoryContextRequest request;
  request.external_evidence = {"external evidence: compile surface"};
  request.retrieval_evidence_refs = {evidence_ref};

  assert_equal(1,
               static_cast<int>(request.external_evidence.size()),
               "memory context request should keep the textual evidence surface");
  assert_equal(1,
               static_cast<int>(request.retrieval_evidence_refs.size()),
               "memory context request should keep the structured evidence surface");

  ContextAssemblyResult result;
  result.context_packet.retrieval_evidence = request.external_evidence;
  result.context_packet.retrieval_evidence_refs = request.retrieval_evidence_refs;

  assert_true(result.context_packet.retrieval_evidence.has_value(),
              "context assembly result should compile with the textual evidence projection");
  assert_true(result.context_packet.retrieval_evidence_refs.has_value(),
              "context assembly result should compile with the structured evidence projection");
  assert_equal("evidence-009-compile",
               result.context_packet.retrieval_evidence_refs->front().evidence_ref,
               "context assembly result should preserve evidence_ref through the public surface");
}

}  // namespace

int main() {
  try {
    test_memory_context_request_exposes_parallel_evidence_projection_surface();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}