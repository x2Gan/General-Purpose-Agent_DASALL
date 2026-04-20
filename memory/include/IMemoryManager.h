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

/// Central facade for the memory subsystem lifecycle.
///
/// Thread-safety contract:
/// - init() and shutdown() must be called from a single owner thread.
/// - prepare_context() and write_back() may be called from any thread after
///   init() returns success, but concurrent calls to write_back() are
///   serialized internally via the shared writer mutex.
/// - export_working_memory_snapshot() is safe to call concurrently with
///   prepare_context(); the working memory board provides its own locking.
/// - run_maintenance() is safe to call concurrently; the maintenance worker
///   acquires the shared writer mutex for checkpoint/retention operations.
class IMemoryManager {
 public:
  virtual ~IMemoryManager() = default;

  virtual contracts::ResultCode init(const MemoryConfig& config) = 0;
  virtual void shutdown() noexcept = 0;

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
