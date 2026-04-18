#include "MemoryManagerInternal.h"

#include <memory>
#include <string>

#include "store/sqlite/SqliteMemoryStore.h"
#include "working/IWorkingMemoryBoard.h"

namespace dasall::memory {
namespace {

class BootstrapContextOrchestrator final : public IContextOrchestrator {
 public:
  [[nodiscard]] ContextAssemblyResult assemble(
      const MemoryContextRequest& request) override {
    ContextAssemblyResult result;
    result.context_packet.request_id = request.request_id;

    if (!request.goal_summary.empty()) {
      result.context_packet.current_goal_summary = request.goal_summary;
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

    result.context_packet.token_budget_report =
        std::string{"token_budget_hint="} +
        std::to_string(request.token_budget_hint);
    result.context_packet.recent_history = std::vector<std::string>{};
    return result;
  }
};

}  // namespace

std::unique_ptr<IMemoryManager> create_memory_manager(const MemoryConfig& config) {
  MemoryManagerDependencies dependencies;
  dependencies.context_orchestrator = std::make_unique<BootstrapContextOrchestrator>();
  dependencies.working_memory_board = create_working_memory_board();
  if (config.storage.backend == "sqlite") {
    dependencies.store = store::sqlite::create_sqlite_memory_store();
  }
  return create_memory_manager_with_dependencies(std::move(dependencies));
}

}  // namespace dasall::memory
