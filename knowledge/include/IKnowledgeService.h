#pragma once

namespace dasall::knowledge {

class IKnowledgeService {
 public:
  virtual ~IKnowledgeService() = 0;

 protected:
  IKnowledgeService() = default;
  IKnowledgeService(const IKnowledgeService&) = default;
  IKnowledgeService& operator=(const IKnowledgeService&) = default;
  IKnowledgeService(IKnowledgeService&&) = default;
  IKnowledgeService& operator=(IKnowledgeService&&) = default;
};

inline IKnowledgeService::~IKnowledgeService() = default;

}  // namespace dasall::knowledge