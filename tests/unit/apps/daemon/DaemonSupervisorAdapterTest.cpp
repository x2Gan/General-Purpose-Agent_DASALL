#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "DaemonSupervisorAdapter.h"
#include "watchdog/HeartbeatSample.h"
#include "watchdog/IWatchdogService.h"
#include "watchdog/WatchedEntityDescriptor.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonSupervisorAdapter;
using dasall::apps::daemon::DaemonSupervisorAdapterOptions;
using dasall::contracts::ResultCode;
using dasall::infra::watchdog::HeartbeatSample;
using dasall::infra::watchdog::IWatchdogService;
using dasall::infra::watchdog::WatchedEntityDescriptor;
using dasall::infra::watchdog::WatchdogOperationResult;
using dasall::infra::watchdog::WatchdogServiceConfig;
using dasall::infra::watchdog::WatchdogSnapshotQueryResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class RecordingWatchdogService final : public IWatchdogService {
 public:
  WatchdogOperationResult register_result = WatchdogOperationResult::success();
  WatchdogOperationResult unregister_result = WatchdogOperationResult::success();
  WatchdogOperationResult heartbeat_result = WatchdogOperationResult::success();

  std::vector<WatchedEntityDescriptor> registered_descriptors;
  std::vector<std::string> unregistered_ids;
  std::vector<HeartbeatSample> heartbeat_samples;

  WatchdogOperationResult init(const WatchdogServiceConfig&) override {
    return WatchdogOperationResult::success();
  }

  WatchdogOperationResult start() override {
    return WatchdogOperationResult::success();
  }

  WatchdogOperationResult stop(std::uint32_t) override {
    return WatchdogOperationResult::success();
  }

  WatchdogOperationResult register_entity(
      const WatchedEntityDescriptor& descriptor) override {
    registered_descriptors.push_back(descriptor);
    return register_result;
  }

  WatchdogOperationResult unregister_entity(
      std::string_view entity_id) override {
    unregistered_ids.emplace_back(entity_id);
    return unregister_result;
  }

  WatchdogOperationResult heartbeat(const HeartbeatSample& sample) override {
    heartbeat_samples.push_back(sample);
    return heartbeat_result;
  }

  [[nodiscard]] WatchdogSnapshotQueryResult snapshot() const override {
    return WatchdogSnapshotQueryResult::failure(
        ResultCode::ValidationFieldMissing,
        "snapshot is not used by DaemonSupervisorAdapterTest",
        "watchdog.snapshot",
        "RecordingWatchdogService");
  }
};

void test_supervisor_adapter_noops_when_watchdog_is_unconfigured() {
  DaemonSupervisorAdapter adapter;

  assert_true(adapter.notify_ready().ok,
              "supervisor adapter should no-op successfully without a configured watchdog bridge");
  assert_true(adapter.tick_watchdog().ok,
              "supervisor adapter should no-op watchdog ticks when watchdog is disabled");
  assert_true(adapter.notify_stopping().ok,
              "supervisor adapter should no-op stop notifications without a configured watchdog bridge");
  assert_true(!adapter.watchdog_active(),
              "supervisor adapter should stay inactive on the v1 no-op path");
}

void test_supervisor_adapter_bridges_ready_tick_and_stopping_to_watchdog_service() {
  auto watchdog_service = std::make_shared<RecordingWatchdogService>();
  DaemonSupervisorAdapter adapter(
      watchdog_service,
      DaemonSupervisorAdapterOptions{
          .watchdog_enabled = true,
          .watchdog_entity_id = "daemon.main_loop",
          .watchdog_timeout_ms = 15000U,
          .watchdog_grace_ms = 2000U,
      });

  const auto ready_result = adapter.notify_ready();
  assert_true(ready_result.ok,
              "supervisor adapter should register the daemon entity when watchdog bridge is enabled");
  assert_true(adapter.watchdog_active(),
              "supervisor adapter should become active after watchdog registration succeeds");
  assert_equal(static_cast<std::size_t>(1U),
               watchdog_service->registered_descriptors.size(),
               "notify_ready should register exactly one watchdog descriptor");
  assert_equal(std::string("daemon.main_loop"),
               watchdog_service->registered_descriptors.front().entity_id,
               "notify_ready should use the configured watchdog entity id");
  assert_equal(std::string("daemon"),
               watchdog_service->registered_descriptors.front().owner_module,
               "notify_ready should freeze daemon as the watchdog owner module");

  const auto tick_result = adapter.tick_watchdog();
  assert_true(tick_result.ok,
              "tick_watchdog should bridge heartbeats to the watchdog service after ready notification");
  assert_equal(static_cast<std::size_t>(1U),
               watchdog_service->heartbeat_samples.size(),
               "tick_watchdog should emit exactly one heartbeat sample per explicit tick");
  assert_equal(std::string("daemon.main_loop"),
               watchdog_service->heartbeat_samples.front().entity_id,
               "tick_watchdog should preserve the configured watchdog entity id");
  assert_equal(static_cast<std::uint64_t>(1U),
               watchdog_service->heartbeat_samples.front().seq_no,
               "tick_watchdog should start heartbeat sequencing from one");
  assert_true(watchdog_service->heartbeat_samples.front().deadline_ts >
                  watchdog_service->heartbeat_samples.front().heartbeat_ts,
              "tick_watchdog should emit a future watchdog deadline");

  const auto stopping_result = adapter.notify_stopping();
  assert_true(stopping_result.ok,
              "notify_stopping should unregister the daemon entity when watchdog bridge is active");
  assert_true(!adapter.watchdog_active(),
              "supervisor adapter should become inactive after successful unregister");
  assert_equal(static_cast<std::size_t>(1U),
               watchdog_service->unregistered_ids.size(),
               "notify_stopping should unregister exactly one watchdog entity");
  assert_equal(std::string("daemon.main_loop"),
               watchdog_service->unregistered_ids.front(),
               "notify_stopping should unregister the configured watchdog entity id");
}

void test_supervisor_adapter_surfaces_watchdog_failures_without_internal_recovery() {
  auto watchdog_service = std::make_shared<RecordingWatchdogService>();
  watchdog_service->heartbeat_result = WatchdogOperationResult::failure(
      ResultCode::RuntimeRetryExhausted,
      "watchdog heartbeat delivery failed",
      "watchdog.heartbeat",
      "RecordingWatchdogService");

  DaemonSupervisorAdapter adapter(
      watchdog_service,
      DaemonSupervisorAdapterOptions{
          .watchdog_enabled = true,
          .watchdog_entity_id = "daemon.main_loop",
          .watchdog_timeout_ms = 15000U,
          .watchdog_grace_ms = 2000U,
      });

  const auto ready_result = adapter.notify_ready();
  assert_true(ready_result.ok,
              "supervisor adapter should still allow ready notification before failure surfacing is exercised");

  const auto tick_result = adapter.tick_watchdog();
  assert_true(!tick_result.ok,
              "tick_watchdog should surface watchdog bridge failures instead of swallowing them");
  assert_true(tick_result.references_only_contract_error_types(),
              "watchdog failures should stay inside contracts ResultCode/ErrorInfo mapping");
  assert_true(adapter.watchdog_active(),
              "watchdog bridge failure should not trigger hidden self-recovery or local deactivation inside daemon");
  assert_equal(static_cast<std::size_t>(1U),
               watchdog_service->heartbeat_samples.size(),
               "tick_watchdog should perform exactly one explicit heartbeat attempt per call");
}

}  // namespace

int main() {
  try {
    test_supervisor_adapter_noops_when_watchdog_is_unconfigured();
    test_supervisor_adapter_bridges_ready_tick_and_stopping_to_watchdog_service();
    test_supervisor_adapter_surfaces_watchdog_failures_without_internal_recovery();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonSupervisorAdapterTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}