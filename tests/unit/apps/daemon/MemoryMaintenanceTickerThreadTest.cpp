#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "MemoryMaintenanceTickerThread.h"
#include "IMemoryManager.h"
#include "audit/IAuditLogger.h"
#include "logging/ILogger.h"
#include "maintenance/BackgroundMaintenanceHooks.h"
#include "support/TestAssertions.h"

namespace {

class FakeMemoryManager final : public dasall::memory::IMemoryManager {
 public:
  enum class Mode {
    Success = 0,
    Warning = 1,
    Exception = 2,
  };

  void push_mode(const Mode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    modes_.push_back(mode);
  }

  [[nodiscard]] int call_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(requests_.size());
  }

  [[nodiscard]] std::vector<dasall::memory::MaintenanceRequest> requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
  }

  dasall::contracts::ResultCode init(const dasall::memory::MemoryConfig&) override {
    return static_cast<dasall::contracts::ResultCode>(0);
  }

  void shutdown() noexcept override {}

  [[nodiscard]] dasall::memory::ContextAssemblyResult prepare_context(
      const dasall::memory::MemoryContextRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::WritebackResult write_back(
      const dasall::memory::MemoryWritebackRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::WorkingMemoryExportResult export_working_memory_snapshot(
      const dasall::memory::WorkingMemoryExportRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::MaintenanceReport run_maintenance(
      const dasall::memory::MaintenanceRequest& request) override {
    Mode mode = Mode::Success;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      requests_.push_back(request);
      if (!modes_.empty()) {
        mode = modes_.front();
        modes_.erase(modes_.begin());
      }
    }

    if (mode == Mode::Exception) {
      throw std::runtime_error("synthetic maintenance failure");
    }

    dasall::memory::MaintenanceReport report;
    report.checkpoint_executed = request.run_checkpoint;
    report.quarantine_cleaned = request.run_quarantine_cleanup ? 1 : 0;
    if (mode == Mode::Warning) {
      report.warnings.push_back("checkpoint_busy");
    }
    return report;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<Mode> modes_;
  std::vector<dasall::memory::MaintenanceRequest> requests_;
};

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  [[nodiscard]] int event_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(events_.size());
  }

  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel) override {}

 private:
  mutable std::mutex mutex_;
  std::vector<dasall::infra::logging::LogEvent> events_;
};

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  [[nodiscard]] int event_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(events_.size());
  }

  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext&) override {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(event);
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return {};
  }

 private:
  mutable std::mutex mutex_;
  std::vector<dasall::infra::AuditEvent> events_;
};

[[nodiscard]] bool wait_for_call_count(const FakeMemoryManager& manager,
                                       const int expected_count,
                                       const std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (manager.call_count() >= expected_count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return manager.call_count() >= expected_count;
}

void test_memory_maintenance_ticker_runs_on_cadence_and_projects_requests() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto manager = std::make_shared<FakeMemoryManager>();
  auto logger = std::make_shared<RecordingLogger>();
  auto audit_logger = std::make_shared<RecordingAuditLogger>();

  std::mutex hook_mutex;
  std::vector<dasall::runtime::RuntimeEventEnvelope> hook_events;
  auto hooks = std::make_shared<dasall::runtime::BackgroundMaintenanceHooks>(
      nullptr,
      dasall::runtime::BackgroundMaintenanceHookOptions{
          .fallback_sink = [&hook_mutex, &hook_events](
                               const dasall::runtime::RuntimeEventEnvelope& event) {
            std::lock_guard<std::mutex> lock(hook_mutex);
            hook_events.push_back(event);
          },
      });

  dasall::apps::daemon::MemoryMaintenanceTickerThread ticker(
      {.memory_manager = manager,
       .background_maintenance_hooks = hooks,
       .logger = logger,
       .audit_logger = audit_logger},
      {.enabled = true,
       .interval_ms = 20,
       .jitter_ms = 0,
       .retention_ms = 500,
       .checkpoint_strategy = "passive_on_retention",
       .failure_backoff_ms = 40});

  assert_true(ticker.start(),
              "memory maintenance ticker should start when manager and config are valid");
  assert_true(wait_for_call_count(*manager, 2, std::chrono::milliseconds(1000)),
              "memory maintenance ticker should execute at least two cadence ticks");
  ticker.stop();

  const auto requests = manager->requests();
  assert_true(requests.size() >= 2U,
              "memory maintenance ticker should record at least two maintenance requests");
  assert_true(requests[0].run_retention,
              "first cadence tick should treat retention as immediately due");
  assert_true(requests[0].run_checkpoint,
              "checkpoint-on-retention strategy should checkpoint the first due tick");
  assert_true(!requests[1].run_retention,
              "second cadence tick should skip retention until the retention window elapses");
  assert_true(!requests[1].run_checkpoint,
              "checkpoint-on-retention strategy should skip checkpoint when retention is not due");
  assert_true(!requests[0].request_id.empty(),
              "ticker should stamp each maintenance request with a request id");

  std::vector<dasall::runtime::RuntimeEventEnvelope> copied_events;
  {
    std::lock_guard<std::mutex> lock(hook_mutex);
    copied_events = hook_events;
  }
  assert_true(copied_events.size() >= 2U,
              "ticker should publish runtime maintenance hook events for each cadence tick");
  assert_equal(std::string("runtime.maintenance.idle_tick"),
               copied_events.front().event_name,
               "ticker should emit the canonical runtime maintenance idle tick event name");
}

void test_memory_maintenance_ticker_applies_failure_backoff_and_audits() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto manager = std::make_shared<FakeMemoryManager>();
  auto logger = std::make_shared<RecordingLogger>();
  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  manager->push_mode(FakeMemoryManager::Mode::Warning);

  dasall::apps::daemon::MemoryMaintenanceTickerThread ticker(
      {.memory_manager = manager,
       .background_maintenance_hooks = nullptr,
       .logger = logger,
       .audit_logger = audit_logger},
      {.enabled = true,
       .interval_ms = 20,
       .jitter_ms = 0,
       .retention_ms = 20,
       .checkpoint_strategy = "passive_each_tick",
       .failure_backoff_ms = 60});

  const auto first_tick = ticker.dispatch_once();
  assert_equal(60,
               static_cast<int>(first_tick.applied_backoff_ms),
               "ticker should apply configured failure backoff when maintenance reports warnings");
  assert_true(first_tick.next_delay_ms >= 80,
              "failure backoff should extend the next tick delay beyond the base cadence");
  assert_equal(1,
               logger->event_count(),
               "failure backoff should emit a diagnostic log event");
  assert_equal(1,
               audit_logger->event_count(),
               "failure backoff should emit an audit event");

  const auto second_tick = ticker.dispatch_once();
  assert_equal(0,
               static_cast<int>(second_tick.applied_backoff_ms),
               "successful follow-up maintenance should clear the transient failure backoff");
  assert_equal(1,
               audit_logger->event_count(),
               "successful follow-up maintenance should not emit extra audit failure events");
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    const std::string selected_test = argc > 1 ? argv[1] : "all";
    if (selected_test == "all" || selected_test == "cadence") {
      test_memory_maintenance_ticker_runs_on_cadence_and_projects_requests();
    }
    if (selected_test == "all" || selected_test == "failure_backoff") {
      test_memory_maintenance_ticker_applies_failure_backoff_and_audits();
    }
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}