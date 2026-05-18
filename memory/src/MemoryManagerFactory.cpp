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
#include "observability/MemoryObservability.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "vector/SimpleLocalEmbeddingAdapter.h"
#include "vector/SqliteVssVectorBackend.h"
#include "vector/UnavailableVectorMemoryIndexAdapter.h"
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

    if (!request.retrieval_evidence_refs.empty()) {
      result.context_packet.retrieval_evidence_refs =
          request.retrieval_evidence_refs;
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

[[nodiscard]] std::unique_ptr<IEmbeddingAdapter> create_embedding_adapter(
    const MemoryConfig& config) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None) {
    return nullptr;
  }

  return std::make_unique<SimpleLocalEmbeddingAdapter>();
}

[[nodiscard]] sqlite3* resolve_sqlite_writer_connection(IMemoryStore& store) {
  auto* sqlite_store =
      dynamic_cast<store::sqlite::SqliteMemoryStore*>(&store);
  if (sqlite_store == nullptr) {
    return nullptr;
  }

  return sqlite_store->writer_connection_for_maintenance();
}

[[nodiscard]] std::unique_ptr<VectorMemoryIndexAdapter> create_vector_index(
    const MemoryConfig& config,
    IMemoryStore& store,
    IEmbeddingAdapter* embedding_adapter) {
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None) {
    return nullptr;
  }

  if (config.vector.backend_type == VectorBackend::SqliteVss) {
    if (sqlite3* db = resolve_sqlite_writer_connection(store); db != nullptr) {
      return std::make_unique<SqliteVssVectorBackend>(
          config.vector, db, embedding_adapter);
    }
  }

  return std::make_unique<UnavailableVectorMemoryIndexAdapter>(
      config.vector, embedding_adapter);
}

}  // namespace

std::unique_ptr<IMemoryManager> create_memory_manager(
    const MemoryConfig& config,
    const MemoryRuntimeDependencies& runtime_dependencies) {
  MemoryManagerDependencies dependencies;
  dependencies.runtime_dependencies = runtime_dependencies;
  dependencies.observability = std::make_shared<observability::MemoryObservability>(
      observability::make_live_telemetry_sink(runtime_dependencies),
      runtime_dependencies.profile_id);
  dependencies.working_memory_board = create_working_memory_board();
  if (config.storage.backend == StorageBackend::Sqlite) {
    dependencies.store = store::sqlite::create_sqlite_memory_store();
    if (dependencies.store) {
      dependencies.store_open_result = dependencies.store->open(config);
      dependencies.store_preopened = !dependencies.store_open_result.has_value();
    }
  }

  if (dependencies.store && dependencies.working_memory_board) {
    dependencies.embedding_adapter = create_embedding_adapter(config);
    dependencies.vector_index = create_vector_index(
        config, *dependencies.store, dependencies.embedding_adapter.get());
    dependencies.store_writer_mutex = std::make_shared<std::mutex>();
    auto collector = std::make_unique<CandidateCollector>(
        *dependencies.working_memory_board, *dependencies.store, *dependencies.store,
        *dependencies.store, *dependencies.store, config,
        dependencies.vector_index.get());
    auto allocator = std::make_unique<BudgetAllocator>(config);
    auto compressor = std::make_unique<CompressionCoordinator>(*dependencies.store);
    auto conflict_resolver =
      std::make_unique<MemoryConflictResolver>(*dependencies.store);
    dependencies.context_orchestrator = std::make_unique<ContextOrchestrator>(
        std::move(collector), std::move(allocator), std::move(compressor),
        config, dependencies.observability);
    dependencies.writeback_coordinator = std::make_unique<WritebackCoordinator>(
      *dependencies.store, *dependencies.store, *dependencies.store,
      *dependencies.store, *dependencies.store, std::move(conflict_resolver),
      *dependencies.working_memory_board, dependencies.vector_index.get(),
      dependencies.store_writer_mutex, dependencies.observability);
    dependencies.maintenance_worker = std::make_unique<MemoryMaintenanceWorker>(
        *dependencies.store, config, dependencies.vector_index.get(),
        dependencies.store_writer_mutex, dependencies.observability);
  } else {
    dependencies.context_orchestrator = std::make_unique<BootstrapContextOrchestrator>();
  }

  return create_memory_manager_with_dependencies(std::move(dependencies));
}

}  // namespace dasall::memory
