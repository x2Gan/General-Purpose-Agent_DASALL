#pragma once

#include <memory>
#include <optional>

#include "IContextOrchestrator.h"
#include "IMemoryManager.h"
#include "IMemoryStore.h"

namespace dasall::memory {

struct MemoryManagerDependencies {
  std::unique_ptr<IContextOrchestrator> context_orchestrator;
  std::unique_ptr<IMemoryStore> store;
};

class MemoryManager final : public IMemoryManager {
 public:
  explicit MemoryManager(MemoryManagerDependencies dependencies);

  [[nodiscard]] contracts::ResultCode init(const MemoryConfig& config) override;
  void shutdown() override;

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
  LifecycleState state_ = LifecycleState::Created;
};

[[nodiscard]] std::unique_ptr<IMemoryManager> create_memory_manager_with_dependencies(
    MemoryManagerDependencies dependencies);

}  // namespace dasall::memory
