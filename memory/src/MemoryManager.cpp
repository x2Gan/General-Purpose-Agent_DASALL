#include "MemoryManagerInternal.h"

#include <utility>

#include "error/MemoryError.h"

namespace dasall::memory {
namespace {

constexpr contracts::ResultCode kMemoryManagerInitSuccess =
    static_cast<contracts::ResultCode>(0);

[[nodiscard]] constexpr contracts::ResultCode config_invalid_code() {
  return map_memory_error(MemoryError::ConfigInvalid).result_code;
}

[[nodiscard]] constexpr contracts::ResultCode storage_unavailable_code() {
  return map_memory_error(MemoryError::StorageUnavailable).result_code;
}

[[nodiscard]] ContextAssemblyResult make_context_failure_result(
    const MemoryContextRequest& request,
    const char* warning_key) {
  ContextAssemblyResult result;
  result.result_code = storage_unavailable_code();
  result.context_packet.request_id =
      request.request_id.empty() ? "context-request" : request.request_id;
  result.context_packet.user_turn = !request.goal_summary.empty()
                                        ? request.goal_summary
                                        : std::string{"context unavailable"};
  result.context_packet.current_goal_summary = !request.goal_summary.empty()
                                                  ? request.goal_summary
                                                  : std::string{"goal unavailable"};
  result.context_packet.recent_history = std::vector<std::string>{};
  if (!request.constraints_summary.empty()) {
    result.context_packet.policy_digest = request.constraints_summary;
  }
  if (!request.latest_observation_digest_summary.empty()) {
    result.context_packet.latest_observation_digest_summary =
        request.latest_observation_digest_summary;
  }
  if (!request.visible_tools.empty()) {
    result.context_packet.active_tools = request.visible_tools;
  }
  result.context_packet.token_budget_report =
      std::string{"token_budget_hint="} +
      std::to_string(request.token_budget_hint);
  result.warnings.push_back(warning_key);
  result.degraded = true;
  return result;
}

[[nodiscard]] WritebackResult make_writeback_failure_result(const char* warning_key) {
  WritebackResult result;
  result.result_code = storage_unavailable_code();
  result.warnings.push_back(warning_key);
  result.degraded = true;
  return result;
}

[[nodiscard]] WorkingMemoryExportResult make_export_failure_result(
    const WorkingMemoryExportRequest& request,
    const char* warning_key) {
  WorkingMemoryExportResult result;
  result.result_code = storage_unavailable_code();
  result.snapshot.session_id = request.session_id;
  result.warnings.push_back(warning_key);
  result.degraded = true;
  return result;
}

[[nodiscard]] WorkingMemoryExportResult make_unwired_export_result(
    const WorkingMemoryExportRequest& request) {
  WorkingMemoryExportResult result;
  result.snapshot.session_id = request.session_id;
  if (request.include_ephemeral_facts) {
    result.warnings.push_back("ephemeral_facts_unavailable");
  }
  result.warnings.push_back("working_memory_board_unwired");
  result.degraded = true;
  return result;
}

[[nodiscard]] MaintenanceReport make_maintenance_report(const char* warning_key) {
  MaintenanceReport report;
  report.warnings.push_back(warning_key);
  return report;
}

}  // namespace

MemoryManager::MemoryManager(MemoryManagerDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

contracts::ResultCode MemoryManager::init(const MemoryConfig& config) {
  if (state_ == LifecycleState::Running) {
    return kMemoryManagerInitSuccess;
  }

  if (config.storage.backend.empty()) {
    return config_invalid_code();
  }

  if (config.storage.backend != "memory" && config.storage.backend != "sqlite") {
    return config_invalid_code();
  }

  if (config.storage.backend == "sqlite" && !dependencies_.store) {
    return storage_unavailable_code();
  }

  if (dependencies_.store) {
    const auto open_result = dependencies_.store->open(config);
    if (open_result.has_value()) {
      return *open_result;
    }
  }

  config_ = config;
  state_ = LifecycleState::Running;
  return kMemoryManagerInitSuccess;
}

void MemoryManager::shutdown() {
  if (state_ == LifecycleState::Running && dependencies_.store) {
    dependencies_.store->close();
  }

  config_.reset();
  state_ = LifecycleState::Stopped;
}

ContextAssemblyResult MemoryManager::prepare_context(
    const MemoryContextRequest& request) {
  if (state_ != LifecycleState::Running) {
    return make_context_failure_result(request, "memory_manager_not_running");
  }

  if (!dependencies_.context_orchestrator) {
    return make_context_failure_result(request, "context_orchestrator_unwired");
  }

  return dependencies_.context_orchestrator->assemble(request);
}

WritebackResult MemoryManager::write_back(const MemoryWritebackRequest& request) {
  if (state_ != LifecycleState::Running) {
    return make_writeback_failure_result("memory_manager_not_running");
  }

  if (request.session_id.empty()) {
    WritebackResult result;
    result.result_code = config_invalid_code();
    result.warnings.push_back("writeback_session_id_missing");
    result.degraded = true;
    return result;
  }

  return make_writeback_failure_result("writeback_pipeline_unwired");
}

WorkingMemoryExportResult MemoryManager::export_working_memory_snapshot(
    const WorkingMemoryExportRequest& request) {
  if (state_ != LifecycleState::Running) {
    return make_export_failure_result(request, "memory_manager_not_running");
  }

  if (!dependencies_.working_memory_board) {
    return make_unwired_export_result(request);
  }

  if (request.evict_expired_before_export) {
    dependencies_.working_memory_board->evict_expired(request.session_id);
  }

  WorkingMemoryExportResult result;
  result.snapshot =
      dependencies_.working_memory_board->export_snapshot(request.session_id);
  if (!request.include_ephemeral_facts) {
    result.snapshot.ephemeral_facts.clear();
  }

  if (result.snapshot.slots.empty() && result.snapshot.open_questions.empty() &&
      result.snapshot.ephemeral_facts.empty()) {
    result.warnings.push_back("session_not_found");
  }

  return result;
}

MaintenanceReport MemoryManager::run_maintenance(
    const MaintenanceRequest& request) {
  if (state_ != LifecycleState::Running) {
    return make_maintenance_report("memory_manager_not_running");
  }

  if (!request.run_checkpoint && !request.run_retention &&
      !request.run_quarantine_cleanup && !request.run_vector_rebuild) {
    return make_maintenance_report("maintenance_noop_requested");
  }

  return make_maintenance_report("maintenance_worker_unwired");
}

std::unique_ptr<IMemoryManager> create_memory_manager_with_dependencies(
    MemoryManagerDependencies dependencies) {
  return std::make_unique<MemoryManager>(std::move(dependencies));
}

}  // namespace dasall::memory
