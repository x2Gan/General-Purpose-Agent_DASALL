#include "KnowledgeEvidenceProjector.h"

#include <algorithm>

namespace dasall::runtime {
namespace {

void append_unique_string(std::vector<std::string>& destination,
                          const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
    destination.push_back(value);
  }
}

void append_retrieval_evidence_ref(
    std::vector<contracts::RetrievalEvidenceRef>& destination,
    const contracts::RetrievalEvidenceRef& value) {
  if (!value.has_consistent_values()) {
    return;
  }

  const auto duplicate = std::find_if(destination.begin(), destination.end(),
                                      [&value](const auto& existing) {
                                        return existing.evidence_ref == value.evidence_ref;
                                      });
  if (duplicate == destination.end()) {
    destination.push_back(value);
  }
}

}  // namespace

void KnowledgeEvidenceProjector::project(
    const knowledge::KnowledgeRetrieveResult& result,
    memory::MemoryContextRequest& request) const {
  if (!result.ok || !result.evidence.has_value()) {
    return;
  }

  for (const auto& projection_line : result.evidence->context_projection) {
    append_unique_string(request.external_evidence, projection_line);
  }

  for (const auto& ref : result.retrieval_evidence_refs) {
    append_retrieval_evidence_ref(request.retrieval_evidence_refs, ref);
  }
}

}  // namespace dasall::runtime