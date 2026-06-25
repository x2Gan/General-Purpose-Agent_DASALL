#pragma once

#include <memory>
#include <string>

#include "ILLMManager.h"
#include "LLMManagerResult.h"
#include "agent/AgentRequest.h"
#include "writeback/MemoryWritebackRequest.h"

namespace dasall::runtime {

class PromptAssetWritebackProjector final {
 public:
  [[nodiscard]] memory::MemoryWritebackRequest enrich_with_prompt_asset(
      memory::MemoryWritebackRequest request,
      const contracts::AgentRequest& agent_request,
      const llm::LLMManagerResult& llm_result,
      const std::shared_ptr<llm::ILLMManager>& llm_manager) const;
};

}  // namespace dasall::runtime