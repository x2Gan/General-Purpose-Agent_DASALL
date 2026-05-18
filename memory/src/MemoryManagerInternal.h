#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "IContextOrchestrator.h"
#include "vector/IEmbeddingAdapter.h"
#include "IMemoryManager.h"
#include "IMemoryStore.h"
#include "maintenance/MemoryMaintenanceWorker.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/WritebackCoordinator.h"

namespace dasall::memory {

struct MemoryManagerDependencies {
  std::unique_ptr<IContextOrchestrator> context_orchestrator;
  std::unique_ptr<IMemoryStore> store;
  std::unique_ptr<IWorkingMemoryBoard> working_memory_board;
  std::unique_ptr<IEmbeddingAdapter> embedding_adapter;
  std::unique_ptr<VectorMemoryIndexAdapter> vector_index;
  bool store_preopened = false;
  std::optional<contracts::ResultCode> store_open_result;
  std::shared_ptr<std::mutex> store_writer_mutex;
  std::unique_ptr<WritebackCoordinator> writeback_coordinator;
  std::unique_ptr<MemoryMaintenanceWorker> maintenance_worker;
};

class MemoryManager final : public IMemoryManager {
 public:
  explicit MemoryManager(MemoryManagerDependencies dependencies);

  [[nodiscard]] contracts::ResultCode init(const MemoryConfig& config) override;
  void shutdown() noexcept override;

  [[nodiscard]] ContextAssemblyResult prepare_context(
      const MemoryContextRequest& request) override;
  [[nodiscard]] WritebackResult write_back(
      const MemoryWritebackRequest& request) override;
  [[nodiscard]] WorkingMemoryExportResult export_working_memory_snapshot(
      const WorkingMemoryExportRequest& request) override;
  [[nodiscard]] MaintenanceReport run_maintenance(
      const MaintenanceRequest& request) override;

 private:
  enum class LifecycleState {
    Created = 0,
    Running = 1,
    Stopped = 2,
  };

  MemoryManagerDependencies dependencies_;
  std::optional<MemoryConfig> config_;
  std::atomic<LifecycleState> state_{LifecycleState::Created};
};

[[nodiscard]] std::unique_ptr<IMemoryManager> create_memory_manager_with_dependencies(
    MemoryManagerDependencies dependencies);

}  // namespace dasall::memory
