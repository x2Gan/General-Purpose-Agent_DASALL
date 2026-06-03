#pragma once

#include "KnowledgeTypes.h"
#include "context/MemoryContextRequest.h"

namespace dasall::runtime {

class KnowledgeEvidenceProjector final {
 public:
  void project(const knowledge::KnowledgeRetrieveResult& result,
               memory::MemoryContextRequest& request) const;
};

}  // namespace dasall::runtime