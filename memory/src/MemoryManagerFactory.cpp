#include "MemoryManagerInternal.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "config/MemoryConfig.h"
#include "conflict/MemoryConflictResolver.h"
#include "context/BudgetAllocator.h"
#include "context/CandidateCollector.h"
#include "context/ContextOrchestrator.h"
#include "maintenance/MemoryMaintenanceWorker.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/CompressionCoordinator.h"
#include "writeback/WritebackCoordinator.h"

namespace dasall::memory {
namespace {

class BootstrapContextOrchestrator final : public IContextOrchestrator {
 public:
  [[nodiscard]] ContextAssemblyResult assemble(
      const MemoryContextRequest& request) override {
    ContextAssemblyResult result;
    result.context_packet.request_id =
        request.request_id.empty() ? "context-request" : request.request_id;
    result.context_packet.user_turn = !request.goal_summary.empty()
                                          ? request.goal_summary
                                          : std::string{"context unavailable"};

    if (!request.goal_summary.empty()) {
      result.context_packet.current_goal_summary = request.goal_summary;
    } else {
      result.context_packet.current_goal_summary = "goal unavailable";
    }

    if (!request.latest_observation_digest_summary.empty()) {
      result.context_packet.latest_observation_digest_summary =
          request.latest_observation_digest_summary;
    }

    if (!request.visible_tools.empty()) {
      result.context_packet.active_tools = request.visible_tools;
    }

    if (!request.external_evidence.empty()) {
      result.context_packet.retrieval_evidence = request.external_evidence;
    }

    if (!request.constraints_summary.empty()) {
      result.context_packet.policy_digest = request.constraints_summary;
    }

    result.context_packet.token_budget_report =
        std::string{"token_budget_hint="} +
        std::to_string(request.token_budget_hint);
    result.context_packet.recent_history = std::vector<std::string>{};
    result.context_packet.created_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
    result.context_packet.tags = std::vector<std::string>{"memory", "bootstrap"};
    return result;
  }
};

}  // namespace

std::unique_ptr<IMemoryManager> create_memory_manager(const MemoryConfig& config) {
  MemoryManagerDependencies dependencies;
  dependencies.working_memory_board = create_working_memory_board();
  if (config.storage.backend == "sqlite") {
    dependencies.store = store::sqlite::create_sqlite_memory_store();
  }

  if (dependencies.store && dependencies.working_memory_board) {
    dependencies.store_writer_mutex = std::make_shared<std::mutex>();
    auto collector = std::make_unique<CandidateCollector>(
        *dependencies.working_memory_board, *dependencies.store, config);
    auto allocator = std::make_unique<BudgetAllocator>(config);
    auto compressor = std::make_unique<CompressionCoordinator>(*dependencies.store);
    auto conflict_resolver =
      std::make_unique<MemoryConflictResolver>(*dependencies.store);
    dependencies.context_orchestrator = std::make_unique<ContextOrchestrator>(
        std::move(collector), std::move(allocator), std::move(compressor), config);
    dependencies.writeback_coordinator = std::make_unique<WritebackCoordinator>(
      *dependencies.store, std::move(conflict_resolver),
      *dependencies.working_memory_board, nullptr,
      dependencies.store_writer_mutex);
    dependencies.maintenance_worker = std::make_unique<MemoryMaintenanceWorker>(
        *dependencies.store, config, nullptr, dependencies.store_writer_mutex);
  } else {
    dependencies.context_orchestrator = std::make_unique<BootstrapContextOrchestrator>();
  }

  return create_memory_manager_with_dependencies(std::move(dependencies));
}

}  // namespace dasall::memory
