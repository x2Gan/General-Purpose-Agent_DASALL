#include "maintenance/MemoryMaintenanceWorker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dasall::memory {
namespace {

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

}  // namespace

MemoryMaintenanceWorker::MemoryMaintenanceWorker(
    IMaintenanceStore& store,
    MemoryConfig config,
    VectorMemoryIndexAdapter* vector_adapter,
    std::shared_ptr<std::mutex> writer_mutex)
    : store_(store),
      config_(std::move(config)),
      vector_adapter_(vector_adapter),
      writer_mutex_(std::move(writer_mutex)) {}

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