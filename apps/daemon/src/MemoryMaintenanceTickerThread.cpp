#include "MemoryMaintenanceTickerThread.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <string>
#include <utility>

#include "IMemoryManager.h"
#include "LogEvent.h"
#include "audit/AuditTypes.h"
#include "audit/IAuditLogger.h"
#include "logging/ILogger.h"
#include "maintenance/BackgroundMaintenanceHooks.h"

namespace dasall::apps::daemon {
namespace {

[[nodiscard]] std::int64_t now_ms() {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string join_warnings(const std::vector<std::string>& warnings) {
  std::string value;
  for (std::size_t index = 0; index < warnings.size(); ++index) {
    if (index > 0U) {
      value.append(",");
    }
    value.append(warnings[index]);
  }
  return value;
}

[[nodiscard]] bool report_has_failure_signal(const memory::MaintenanceReport& report) {
  return !report.warnings.empty();
}

[[nodiscard]] std::string make_request_id(const std::uint64_t tick_sequence) {
  return "daemon-memory-maintenance-" + std::to_string(tick_sequence);
}

[[nodiscard]] std::string make_trace_id(const std::uint64_t tick_sequence) {
  return "daemon:memory-maintenance:" + std::to_string(tick_sequence);
}

void emit_failure_log(const std::shared_ptr<infra::logging::ILogger>& logger,
                      const std::uint64_t tick_sequence,
                      const std::int64_t next_backoff_ms,
                      const std::string& detail,
                      const std::size_t warning_count,
                      const bool exception_raised) {
  if (logger == nullptr) {
    return;
  }

  infra::LogEvent event;
  event.level = exception_raised ? infra::LogLevel::Error : infra::LogLevel::Warn;
  event.module = "daemon.memory_maintenance";
  event.message = exception_raised
                      ? "daemon memory maintenance ticker failed"
                      : "daemon memory maintenance ticker reported warnings";
  event.attrs.emplace("tick_sequence", std::to_string(tick_sequence));
  event.attrs.emplace("next_backoff_ms", std::to_string(next_backoff_ms));
  event.attrs.emplace("warning_count", std::to_string(warning_count));
  if (!detail.empty()) {
    event.attrs.emplace("detail", detail);
  }
  event.ts = now_ms();
  (void)logger->log(event);
}

void emit_failure_audit(const std::shared_ptr<infra::audit::IAuditLogger>& audit_logger,
                        const std::uint64_t tick_sequence,
                        const std::int64_t next_backoff_ms,
                        const std::string& detail) {
  if (audit_logger == nullptr) {
    return;
  }

  infra::AuditEvent event;
  event.event_id = "daemon.memory.maintenance." + std::to_string(tick_sequence);
  event.action = "daemon.memory_maintenance.tick";
  event.actor = "daemon";
  event.target = "memory.maintenance_ticker";
  event.outcome = infra::AuditOutcome::Escalated;
  event.evidence_ref = infra::AuditEvidenceRef{
      .kind = infra::AuditEvidenceKind::WorkerTask,
      .ref = "memory-maintenance-ticker",
  };
  event.side_effects.push_back("next_backoff_ms=" + std::to_string(next_backoff_ms));
  if (!detail.empty()) {
    event.side_effects.push_back("detail=" + detail);
  }
  event.timestamp = now_ms();

  (void)audit_logger->write_audit(event, infra::AuditContext{});
}

}  // namespace

bool MemoryMaintenanceTickerConfig::has_consistent_values() const {
  return interval_ms > 0 && jitter_ms >= 0 && retention_ms >= 0 &&
         failure_backoff_ms >= 0 &&
         profiles::MemoryMaintenancePolicy::is_supported_checkpoint_strategy(
             checkpoint_strategy);
}

MemoryMaintenanceTickerConfig project_memory_maintenance_ticker_config(
    const profiles::RuntimePolicySnapshot& snapshot) {
  const auto& policy = snapshot.memory_maintenance_policy();
  return MemoryMaintenanceTickerConfig{
      .enabled = policy.enabled,
      .interval_ms = policy.interval_ms,
      .jitter_ms = policy.jitter_ms,
      .retention_ms = policy.retention_ms,
      .checkpoint_strategy = policy.checkpoint_strategy,
      .failure_backoff_ms = std::max<std::int64_t>(policy.interval_ms, 1000),
  };
}

MemoryMaintenanceTickerThread::MemoryMaintenanceTickerThread(
    Dependencies dependencies,
    MemoryMaintenanceTickerConfig config)
    : dependencies_(std::move(dependencies)),
      config_(std::move(config)),
      pending_delay_ms_(initial_delay_ms()) {}

MemoryMaintenanceTickerThread::~MemoryMaintenanceTickerThread() {
  stop();
}

bool MemoryMaintenanceTickerThread::start() noexcept {
  if (!config_.enabled || !config_.has_consistent_values() ||
      dependencies_.memory_manager == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(control_mutex_);
  if (running_.load()) {
    return true;
  }

  stop_requested_ = false;
  pending_delay_ms_ = initial_delay_ms();
  try {
    worker_ = std::thread([this]() { run_loop(); });
    running_.store(true);
    return true;
  } catch (...) {
    stop_requested_ = true;
    return false;
  }
}

void MemoryMaintenanceTickerThread::stop() noexcept {
  {
    std::lock_guard<std::mutex> lock(control_mutex_);
    stop_requested_ = true;
  }
  control_cv_.notify_all();

  if (worker_.joinable()) {
    worker_.join();
  }

  running_.store(false);
}

bool MemoryMaintenanceTickerThread::running() const noexcept {
  return running_.load();
}

MemoryMaintenanceTickResult MemoryMaintenanceTickerThread::dispatch_once() {
  MemoryMaintenanceTickResult result;
  if (!config_.enabled || !config_.has_consistent_values()) {
    result.next_delay_ms = 0;
    result.report.warnings.push_back("memory_maintenance_ticker_disabled");
    return result;
  }

  const auto current_now_ms = now_ms();

  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    result.tick_sequence = ++tick_sequence_;
    result.retention_due = config_.retention_ms <= 0 || next_retention_due_ms_ <= 0 ||
                           current_now_ms >= next_retention_due_ms_;
    if (result.retention_due && config_.retention_ms > 0) {
      next_retention_due_ms_ = current_now_ms + config_.retention_ms;
    }
  }

  result.request = memory::MaintenanceRequest{
      .run_checkpoint = should_run_checkpoint(result.retention_due),
      .run_retention = result.retention_due,
      .run_quarantine_cleanup = result.retention_due,
      .run_vector_rebuild = false,
      .request_id = make_request_id(result.tick_sequence),
      .trace_id = make_trace_id(result.tick_sequence),
  };

  if (dependencies_.background_maintenance_hooks != nullptr) {
    (void)dependencies_.background_maintenance_hooks->publish_idle_tick(
        runtime::BackgroundMaintenanceTick{
            .tick_sequence = result.tick_sequence,
            .system_idle = true,
            .checkpoint_cleanup_due = result.request.run_checkpoint,
            .session_expiry_due = result.request.run_retention ||
                                  result.request.run_quarantine_cleanup,
            .health_probe_due = false,
            .profile_refresh_due = false,
            .telemetry_flush_due = false,
            .detail = "daemon memory maintenance ticker",
            .timestamp_ms = current_now_ms,
        });
  }

  bool failure = false;
  bool exception_raised = false;
  std::string failure_detail;

  if (dependencies_.memory_manager == nullptr) {
    failure = true;
    failure_detail = "memory manager unavailable";
    result.report.warnings.push_back("memory_manager_unavailable");
  } else {
    try {
      result.report = dependencies_.memory_manager->run_maintenance(result.request);
      failure = report_has_failure_signal(result.report);
      failure_detail = join_warnings(result.report.warnings);
    } catch (const std::exception& exception) {
      failure = true;
      exception_raised = true;
      failure_detail = exception.what();
      result.report.warnings.push_back("memory_maintenance_exception");
    } catch (...) {
      failure = true;
      exception_raised = true;
      failure_detail = "unknown exception";
      result.report.warnings.push_back("memory_maintenance_exception");
    }
  }

  result.applied_backoff_ms = failure ? config_.failure_backoff_ms : 0;
  result.next_delay_ms = std::max<std::int64_t>(
      1,
      config_.interval_ms + compute_jitter_ms(result.tick_sequence + 1U) +
          result.applied_backoff_ms);

  if (failure) {
    emit_failure_log(dependencies_.logger,
                     result.tick_sequence,
                     result.applied_backoff_ms,
                     failure_detail,
                     result.report.warnings.size(),
                     exception_raised);
    emit_failure_audit(dependencies_.audit_logger,
                       result.tick_sequence,
                       result.applied_backoff_ms,
                       failure_detail);
  }

  return result;
}

void MemoryMaintenanceTickerThread::run_loop() {
  for (;;) {
    std::unique_lock<std::mutex> lock(control_mutex_);
    const auto wait_ms = std::max<std::int64_t>(1, pending_delay_ms_);
    if (control_cv_.wait_for(lock,
                             std::chrono::milliseconds(wait_ms),
                             [this]() { return stop_requested_; })) {
      break;
    }
    lock.unlock();

    const auto result = dispatch_once();

    lock.lock();
    pending_delay_ms_ = result.next_delay_ms;
  }

  running_.store(false);
}

bool MemoryMaintenanceTickerThread::should_run_checkpoint(
    const bool retention_due) const {
  if (config_.checkpoint_strategy == "passive_each_tick") {
    return true;
  }
  if (config_.checkpoint_strategy == "passive_on_retention") {
    return retention_due;
  }
  return false;
}

std::int64_t MemoryMaintenanceTickerThread::initial_delay_ms() const {
  if (!config_.has_consistent_values()) {
    return 0;
  }
  return std::max<std::int64_t>(1, config_.interval_ms + compute_jitter_ms(1U));
}

std::int64_t MemoryMaintenanceTickerThread::compute_jitter_ms(
    const std::uint64_t tick_sequence) const {
  if (config_.jitter_ms <= 0) {
    return 0;
  }

  const auto jitter_window = static_cast<std::uint64_t>(config_.jitter_ms) + 1U;
  return static_cast<std::int64_t>(tick_sequence % jitter_window);
}

}  // namespace dasall::apps::daemon