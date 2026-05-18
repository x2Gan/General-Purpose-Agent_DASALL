#pragma once

#include <memory>
#include <mutex>

#include "IExperienceStore.h"
#include "IFactStore.h"
#include "ISessionStore.h"
#include "ISummaryStore.h"
#include "ITransactionalStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

namespace dasall::memory {

namespace observability {

class MemoryObservability;

}  // namespace observability

class WritebackCoordinator {
 public:
  WritebackCoordinator(ITransactionalStore& transaction_store,
                       ISessionStore& session_store,
                       ISummaryStore& summary_store,
                       IFactStore& fact_store,
                       IExperienceStore& experience_store,
                       std::unique_ptr<MemoryConflictResolver> conflict_resolver,
                       IWorkingMemoryBoard& working_memory_board,
                       VectorMemoryIndexAdapter* vector_index = nullptr,
                       std::shared_ptr<std::mutex> writer_mutex = nullptr,
                       std::shared_ptr<observability::MemoryObservability> observability = nullptr);

  [[nodiscard]] WritebackResult persist(const MemoryWritebackRequest& request);

 private:
  [[nodiscard]] WritebackResult persist_core_transaction(
      const MemoryWritebackRequest& request);

  void persist_derived_data(const MemoryWritebackRequest& request,
                            WritebackResult& result);

  void persist_vector_sidecar(const MemoryWritebackRequest& request,
                              WritebackResult& result);

  void update_working_board(const MemoryWritebackRequest& request,
                            const WritebackResult& result);

  ITransactionalStore& transaction_store_;
  ISessionStore& session_store_;
  ISummaryStore& summary_store_;
  IFactStore& fact_store_;
  IExperienceStore& experience_store_;
  std::unique_ptr<MemoryConflictResolver> conflict_resolver_;
  IWorkingMemoryBoard& working_memory_board_;
  VectorMemoryIndexAdapter* vector_index_ = nullptr;
  std::shared_ptr<std::mutex> writer_mutex_;
  std::shared_ptr<observability::MemoryObservability> observability_;
};

}  // namespace dasall::memory