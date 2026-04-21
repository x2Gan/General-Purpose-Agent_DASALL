#pragma once

#include "KnowledgeTypes.h"

namespace dasall::knowledge {

class IKnowledgeService {
 public:
  virtual ~IKnowledgeService() = default;

  virtual bool init(const KnowledgeConfigSnapshot& config) = 0;
  virtual KnowledgeRetrieveResult retrieve(const KnowledgeQuery& query) = 0;
  virtual KnowledgeHealthSnapshot health_snapshot() const = 0;
  virtual RefreshResult request_refresh(const CorpusChangeSet& changes) = 0;
};

}  // namespace dasall::knowledge