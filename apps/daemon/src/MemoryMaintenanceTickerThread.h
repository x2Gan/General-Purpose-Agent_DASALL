#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "MaintenanceReport.h"
#include "MaintenanceRequest.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::infra::audit {

class IAuditLogger;

}  // namespace dasall::infra::audit

namespace dasall::infra::logging {

class ILogger;

}  // namespace dasall::infra::logging

namespace dasall::memory {

class IMemoryManager;

}  // namespace dasall::memory

namespace dasall::runtime {

class BackgroundMaintenanceHooks;

}  // namespace dasall::runtime

namespace dasall::apps::daemon {

struct MemoryMaintenanceTickerConfig {
  bool enabled = false;
  std::int64_t interval_ms = 0;
  std::int64_t jitter_ms = 0;
  std::int64_t retention_ms = 0;
  std::string checkpoint_strategy = "disabled";
  std::int64_t failure_backoff_ms = 0;

  [[nodiscard]] bool has_consistent_values() const;
};

[[nodiscard]] MemoryMaintenanceTickerConfig project_memory_maintenance_ticker_config(
    const profiles::RuntimePolicySnapshot& snapshot);

struct MemoryMaintenanceTickResult {
  std::uint64_t tick_sequence = 0U;
  bool retention_due = false;
  std::int64_t next_delay_ms = 0;
  std::int64_t applied_backoff_ms = 0;
  memory::MaintenanceRequest request;
  memory::MaintenanceReport report;
};

class MemoryMaintenanceTickerThread final {
 public:
  struct Dependencies {
    std::shared_ptr<memory::IMemoryManager> memory_manager;
    std::shared_ptr<runtime::BackgroundMaintenanceHooks> background_maintenance_hooks;
    std::shared_ptr<infra::logging::ILogger> logger;
    std::shared_ptr<infra::audit::IAuditLogger> audit_logger;
  };

  MemoryMaintenanceTickerThread(Dependencies dependencies,
                                MemoryMaintenanceTickerConfig config);
  ~MemoryMaintenanceTickerThread();

  MemoryMaintenanceTickerThread(const MemoryMaintenanceTickerThread&) = delete;
  MemoryMaintenanceTickerThread& operator=(const MemoryMaintenanceTickerThread&) = delete;

  [[nodiscard]] bool start() noexcept;
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] MemoryMaintenanceTickResult dispatch_once();

 private:
  void run_loop();

  [[nodiscard]] bool should_run_checkpoint(bool retention_due) const;
  [[nodiscard]] std::int64_t initial_delay_ms() const;
  [[nodiscard]] std::int64_t compute_jitter_ms(std::uint64_t tick_sequence) const;

  Dependencies dependencies_;
  MemoryMaintenanceTickerConfig config_;

  mutable std::mutex control_mutex_;
  std::condition_variable control_cv_;
  std::thread worker_;
  bool stop_requested_ = false;
  std::int64_t pending_delay_ms_ = 0;
  std::atomic<bool> running_{false};

  mutable std::mutex state_mutex_;
  std::uint64_t tick_sequence_ = 0U;
  std::int64_t next_retention_due_ms_ = 0;
};

}  // namespace dasall::apps::daemon