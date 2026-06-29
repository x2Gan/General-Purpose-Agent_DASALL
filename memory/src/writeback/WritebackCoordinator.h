#pragma once

#include <memory>
#include <mutex>

#include "IMemoryStore.h"
#include "ITransactionalStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/HierarchicalSummarizationCoordinator.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

namespace dasall::memory {

namespace observability {

class MemoryObservability;
class MemoryQualityProbe;

}  // namespace observability

class WritebackCoordinator {
 public:
  WritebackCoordinator(ITransactionalStore& transaction_store,
                       IMemoryStore& store,
                       std::unique_ptr<HierarchicalSummarizationCoordinator> hierarchy_coordinator,
                       std::unique_ptr<MemoryConflictResolver> conflict_resolver,
                       IWorkingMemoryBoard& working_memory_board,
                       VectorMemoryIndexAdapter* vector_index = nullptr,
                       std::shared_ptr<std::mutex> writer_mutex = nullptr,
                       std::shared_ptr<observability::MemoryObservability> observability = nullptr,
                       std::shared_ptr<observability::MemoryQualityProbe> quality_probe = nullptr);

  [[nodiscard]] WritebackResult persist(const MemoryWritebackRequest& request);

 private:
  [[nodiscard]] WritebackResult persist_core_transaction(
      const MemoryWritebackRequest& request);

  void persist_derived_data(const MemoryWritebackRequest& request,
                            WritebackResult& result,
                            std::uint64_t& attempted_conflicts,
                            std::uint64_t& resolved_conflicts);

  void persist_vector_sidecar(const MemoryWritebackRequest& request,
                              WritebackResult& result);

  void update_working_board(const MemoryWritebackRequest& request,
                            const WritebackResult& result);

  ITransactionalStore& transaction_store_;
  IMemoryStore& store_;
  std::unique_ptr<HierarchicalSummarizationCoordinator> hierarchy_coordinator_;
  std::unique_ptr<MemoryConflictResolver> conflict_resolver_;
  IWorkingMemoryBoard& working_memory_board_;
  VectorMemoryIndexAdapter* vector_index_ = nullptr;
  std::shared_ptr<std::mutex> writer_mutex_;
  std::shared_ptr<observability::MemoryObservability> observability_;
  std::shared_ptr<observability::MemoryQualityProbe> quality_probe_;
};

}  // namespace dasall::memory