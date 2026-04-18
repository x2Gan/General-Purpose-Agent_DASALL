#pragma once

#include <memory>

#include "MaintenanceReport.h"
#include "MaintenanceRequest.h"
#include "config/MemoryConfig.h"
#include "context/ContextAssemblyResult.h"
#include "context/MemoryContextRequest.h"
#include "error/ResultCode.h"
#include "working/WorkingMemoryExportRequest.h"
#include "working/WorkingMemoryExportResult.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

namespace dasall::memory {

class IMemoryManager {
 public:
  virtual ~IMemoryManager() = default;

  virtual contracts::ResultCode init(const MemoryConfig& config) = 0;
  virtual void shutdown() = 0;

  [[nodiscard]] virtual ContextAssemblyResult prepare_context(
      const MemoryContextRequest& request) = 0;

  [[nodiscard]] virtual WritebackResult write_back(
      const MemoryWritebackRequest& request) = 0;

  [[nodiscard]] virtual WorkingMemoryExportResult export_working_memory_snapshot(
      const WorkingMemoryExportRequest& request) = 0;

  [[nodiscard]] virtual MaintenanceReport run_maintenance(
      const MaintenanceRequest& request) = 0;
};

[[nodiscard]] std::unique_ptr<IMemoryManager> create_memory_manager(
        const MemoryConfig& config);

}  // namespace dasall::memory
