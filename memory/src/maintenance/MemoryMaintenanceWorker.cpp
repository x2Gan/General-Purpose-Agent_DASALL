#include "maintenance/MemoryMaintenanceWorker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "observability/MemoryObservability.h"

namespace dasall::memory {
namespace {

using observability::MemoryTelemetryContext;
using observability::MemoryTelemetryField;

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void append_warning_once(std::vector<std::string>& warnings,
                         std::string warning) {
  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(std::move(warning));
  }
}

void run_vector_rebuild(VectorMemoryIndexAdapter* vector_adapter,
                        MaintenanceReport& report) {
  if (vector_adapter == nullptr || !vector_adapter->is_available()) {
    append_warning_once(report.warnings, "vector_rebuild_skipped");
    return;
  }

  const auto rebuild_result = vector_adapter->rebuild_index();
  if (!rebuild_result.ok) {
    append_warning_once(report.warnings, "vector_rebuild_failed");
    return;
  }

  report.vector_rebuild_executed = true;
}

[[nodiscard]] MemoryTelemetryContext make_observability_context() {
  return MemoryTelemetryContext{
      .request_id = "maintenance",
      .session_id = {},
      .stage = "maintenance",
      .trace_id = {},
      .profile_id = {},
  };
}

[[nodiscard]] std::vector<MemoryTelemetryField> make_maintenance_fields(
    const MaintenanceRequest& request,
    const MaintenanceReport& report) {
  std::vector<MemoryTelemetryField> fields;
  fields.push_back(MemoryTelemetryField{.key = "checkpoint_requested",
                                        .value = request.run_checkpoint ? "true" : "false"});
  fields.push_back(MemoryTelemetryField{.key = "retention_requested",
                                        .value = request.run_retention ? "true" : "false"});
  fields.push_back(MemoryTelemetryField{.key = "quarantine_requested",
                                        .value = request.run_quarantine_cleanup ? "true" : "false"});
  fields.push_back(MemoryTelemetryField{.key = "vector_rebuild_requested",
                                        .value = request.run_vector_rebuild ? "true" : "false"});
  fields.push_back(MemoryTelemetryField{.key = "checkpoint_executed",
                                        .value = report.checkpoint_executed ? "true" : "false"});
  fields.push_back(MemoryTelemetryField{.key = "turns_purged",
                                        .value = std::to_string(report.turns_purged)});
  fields.push_back(MemoryTelemetryField{.key = "facts_purged",
                                        .value = std::to_string(report.facts_purged)});
  fields.push_back(MemoryTelemetryField{.key = "experiences_purged",
                                        .value = std::to_string(report.experiences_purged)});
  fields.push_back(MemoryTelemetryField{.key = "quarantine_cleaned",
                                        .value = std::to_string(report.quarantine_cleaned)});
  fields.push_back(MemoryTelemetryField{.key = "warning_count",
                                        .value = std::to_string(report.warnings.size())});
  if (!report.warnings.empty()) {
    fields.push_back(MemoryTelemetryField{.key = "warning",
                                          .value = report.warnings.front()});
  }
  fields.push_back(MemoryTelemetryField{.key = "duration_ms",
                                        .value = std::to_string(report.duration_ms)});
  return fields;
}

}  // namespace

MemoryMaintenanceWorker::MemoryMaintenanceWorker(
    IMaintenanceStore& store,
    MemoryConfig config,
    VectorMemoryIndexAdapter* vector_adapter,
    std::shared_ptr<std::mutex> writer_mutex,
    std::shared_ptr<observability::MemoryObservability> observability)
    : store_(store),
      config_(std::move(config)),
      vector_adapter_(vector_adapter),
      writer_mutex_(std::move(writer_mutex)),
      observability_(std::move(observability)) {}

MemoryMaintenanceWorker::~MemoryMaintenanceWorker() {
  stop();
}

void MemoryMaintenanceWorker::start() {
  std::lock_guard<std::mutex> lock(schedule_mutex_);
  if (started_ || !config_.maintenance.auto_schedule) {
    return;
  }

  stopped_ = false;
  started_ = true;
  worker_thread_ = std::thread(&MemoryMaintenanceWorker::background_loop, this);
}

void MemoryMaintenanceWorker::stop() {
  {
    std::lock_guard<std::mutex> lock(schedule_mutex_);
    stopped_ = true;
  }
  schedule_cv_.notify_one();

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }

  std::lock_guard<std::mutex> lock(schedule_mutex_);
  started_ = false;
}

MaintenanceReport MemoryMaintenanceWorker::execute(
    const MaintenanceRequest& request) {
  std::unique_lock<std::mutex> writer_lock;
  if (writer_mutex_) {
    writer_lock = std::unique_lock<std::mutex>(*writer_mutex_);
  }

  MaintenanceReport report;
  const auto started_at = current_time_millis();

  if (request.run_checkpoint) {
    store_.run_wal_checkpoint(config_, report);
  }

  if (request.run_retention) {
    report.turns_purged = store_.run_turn_retention(config_, report);
    report.facts_purged = store_.run_fact_retention(config_, report);
    report.experiences_purged = store_.run_experience_retention(config_, report);
  }

  if (request.run_quarantine_cleanup) {
    report.quarantine_cleaned = store_.run_quarantine_cleanup(config_, report);
  }

  if (request.run_vector_rebuild) {
    run_vector_rebuild(vector_adapter_, report);
  }

  report.duration_ms = current_time_millis() - started_at;
  if (observability_) {
    observability_->emit(report.warnings.empty() ? "maintenance.completed"
                                                 : "maintenance.degraded",
                         make_observability_context(),
                         make_maintenance_fields(request, report));
  }
  return report;
}

void MemoryMaintenanceWorker::background_loop() {
  const auto interval =
      std::chrono::milliseconds(std::max(std::int64_t{1}, config_.maintenance.schedule_interval_ms));
  const MaintenanceRequest scheduled_request{};

  std::unique_lock<std::mutex> lock(schedule_mutex_);
  while (!stopped_) {
    if (schedule_cv_.wait_for(lock, interval, [this]() { return stopped_; })) {
      break;
    }

    lock.unlock();
    (void)execute(scheduled_request);
    lock.lock();
  }
}

}  // namespace dasall::memory