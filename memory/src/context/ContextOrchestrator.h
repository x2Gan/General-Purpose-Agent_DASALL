#pragma once

#include <memory>

#include "IContextOrchestrator.h"
#include "config/MemoryConfig.h"
#include "context/BudgetAllocator.h"
#include "context/CandidateCollector.h"
#include "writeback/CompressionCoordinator.h"

namespace dasall::memory {

namespace observability {

class MemoryObservability;

}  // namespace observability

class ContextOrchestrator final : public IContextOrchestrator {
 public:
  ContextOrchestrator(std::unique_ptr<CandidateCollector> collector,
                      std::unique_ptr<BudgetAllocator> allocator,
                      std::unique_ptr<CompressionCoordinator> compressor,
                      const MemoryConfig& config,
                      std::shared_ptr<observability::MemoryObservability> observability = nullptr);

  [[nodiscard]] ContextAssemblyResult assemble(
      const MemoryContextRequest& request) override;

 private:
  [[nodiscard]] contracts::ContextPacket build_packet(
      const CandidateSet& candidates,
      const BudgetPlan& plan,
      const MemoryContextRequest& request) const;
  [[nodiscard]] bool needs_compression(const CandidateSet& candidates,
                                       int token_budget_hint) const;

  std::unique_ptr<CandidateCollector> collector_;
  std::unique_ptr<BudgetAllocator> allocator_;
  std::unique_ptr<CompressionCoordinator> compressor_;
  ContextConfig context_config_{};
    std::shared_ptr<observability::MemoryObservability> observability_;
};

}  // namespace dasall::memory