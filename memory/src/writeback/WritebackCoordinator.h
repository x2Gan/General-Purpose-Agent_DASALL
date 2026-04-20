#pragma once

#include <memory>
#include <mutex>

#include "IMemoryStore.h"
#include "conflict/MemoryConflictResolver.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

namespace dasall::memory {

class WritebackCoordinator {
 public:
  WritebackCoordinator(IMemoryStore& store,
                       std::unique_ptr<MemoryConflictResolver> conflict_resolver,
                       IWorkingMemoryBoard& working_memory_board,
                       VectorMemoryIndexAdapter* vector_index = nullptr,
                       std::shared_ptr<std::mutex> writer_mutex = nullptr);

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

  IMemoryStore& store_;
  std::unique_ptr<MemoryConflictResolver> conflict_resolver_;
  IWorkingMemoryBoard& working_memory_board_;
  VectorMemoryIndexAdapter* vector_index_ = nullptr;
  std::shared_ptr<std::mutex> writer_mutex_;
};

}  // namespace dasall::memory