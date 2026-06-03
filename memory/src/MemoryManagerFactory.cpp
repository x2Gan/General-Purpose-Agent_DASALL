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
#include "util/TokenEstimator.h"
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

struct EmbeddingAdapterSelection {
  std::unique_ptr<IEmbeddingAdapter> adapter;
  bool used_local_fallback = false;
  const char* warning_key = nullptr;
  const char* fallback_reason = nullptr;
};

[[nodiscard]] const char* vector_backend_name(VectorBackend backend) {
  switch (backend) {
    case VectorBackend::SqliteVss:
      return "sqlite_vss";
    case VectorBackend::None:
      return "none";
  }

  return "unknown";
}

void emit_embedding_adapter_fallback_warning(
    const std::shared_ptr<observability::MemoryObservability>& observability,
    const MemoryRuntimeDependencies& runtime_dependencies,
    const MemoryConfig& config,
    const EmbeddingAdapterSelection& selection) {
  if (observability == nullptr || !selection.used_local_fallback ||
      selection.warning_key == nullptr) {
    return;
  }

  observability->emit(
      "factory.embedding_adapter.degraded",
      observability::MemoryTelemetryContext{
          .request_id = "memory-factory-embedding-adapter",
          .session_id = {},
          .stage = "factory",
          .trace_id = {},
          .profile_id = runtime_dependencies.profile_id,
      },
      {
          observability::MemoryTelemetryField{.key = "warning_count",
                                              .value = "1"},
          observability::MemoryTelemetryField{.key = "warning",
                                              .value = selection.warning_key},
          observability::MemoryTelemetryField{.key = "warning_codes",
                                              .value = selection.warning_key},
          observability::MemoryTelemetryField{.key = "vector_enabled",
                                              .value = config.vector.enabled ? "true"
                                                                            : "false"},
          observability::MemoryTelemetryField{.key = "reason",
                                              .value = selection.fallback_reason == nullptr
                                                           ? "unknown"
                                                           : selection.fallback_reason},
          observability::MemoryTelemetryField{.key = "storage_backend",
                                              .value = vector_backend_name(
                                                  config.vector.backend_type)},
      });
}

[[nodiscard]] EmbeddingAdapterSelection create_embedding_adapter(
    const MemoryConfig& config,
    const MemoryRuntimeDependencies& runtime_dependencies) {
  EmbeddingAdapterSelection selection;
  if (!config.vector.enabled ||
      config.vector.backend_type == VectorBackend::None) {
    return selection;
  }

  if (runtime_dependencies.embedding_adapter_factory) {
    selection.adapter = runtime_dependencies.embedding_adapter_factory(config);
    if (selection.adapter != nullptr) {
      return selection;
    }

    selection.used_local_fallback = true;
    selection.warning_key = "embedding_adapter_local_fallback";
    selection.fallback_reason = "factory_returned_null";
  } else {
    selection.used_local_fallback = true;
    selection.warning_key = "embedding_adapter_local_fallback";
    selection.fallback_reason = "factory_missing";
  }

  selection.adapter = std::make_unique<SimpleLocalEmbeddingAdapter>();
  return selection;
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
    if (runtime_dependencies.summarizer_factory) {
      dependencies.summarizer = runtime_dependencies.summarizer_factory(config);
    }
    auto embedding_adapter_selection = create_embedding_adapter(
      config, runtime_dependencies);
    emit_embedding_adapter_fallback_warning(
      dependencies.observability,
      runtime_dependencies,
      config,
      embedding_adapter_selection);
    const auto token_estimator = util::create_token_estimator(config);
    dependencies.embedding_adapter =
      std::move(embedding_adapter_selection.adapter);
    dependencies.vector_index = create_vector_index(
        config, *dependencies.store, dependencies.embedding_adapter.get());
    dependencies.store_writer_mutex = std::make_shared<std::mutex>();
    auto collector = std::make_unique<CandidateCollector>(
        *dependencies.working_memory_board, *dependencies.store, *dependencies.store,
        *dependencies.store, *dependencies.store, config,
        dependencies.vector_index.get(), token_estimator);
    auto allocator = std::make_unique<BudgetAllocator>(config, token_estimator);
    auto compressor = std::make_unique<CompressionCoordinator>(
        *dependencies.store, dependencies.summarizer.get(), token_estimator);
    auto conflict_resolver =
      std::make_unique<MemoryConflictResolver>(*dependencies.store);
    dependencies.context_orchestrator = std::make_unique<ContextOrchestrator>(
        std::move(collector), std::move(allocator), std::move(compressor),
        config, dependencies.observability, token_estimator);
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
