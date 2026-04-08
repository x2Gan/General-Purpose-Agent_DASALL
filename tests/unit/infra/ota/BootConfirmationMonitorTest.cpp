#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ota/BootConfirmationMonitor.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::HealthListenerSubscriptionResult;
using dasall::infra::HealthMonitorRegistrationResult;
using dasall::infra::HealthProbeRegistration;
using dasall::infra::HealthSnapshot;
using dasall::infra::HealthSnapshotResult;
using dasall::infra::IHealthMonitor;
using dasall::infra::IHealthStateListener;
using dasall::infra::ota::BootConfirmationMonitor;
using dasall::infra::ota::BootConfirmationPolicySnapshot;
using dasall::infra::ota::BootConfirmationRequest;
using dasall::infra::ota::BootConfirmationState;
using dasall::infra::ota::BootMutationResult;
using dasall::infra::ota::BootSuccessSignal;
using dasall::infra::ota::IBootControlAdapter;
using dasall::infra::ota::IBootSuccessSignalProvider;
using dasall::infra::ota::IHeartbeatFreshnessSource;
using dasall::infra::ota::IVersionReportSource;
using dasall::infra::ota::RollbackToken;
using dasall::infra::ota::SlotPlan;
using dasall::infra::ota::VersionReportSnapshot;

BootConfirmationRequest make_request() {
  return BootConfirmationRequest{
      .plan_id = std::string("ota-plan-011"),
      .package_id = std::string("package-011"),
      .slot_plan = SlotPlan{
          .active_slot = std::string("rootfs_a"),
          .target_slot = std::string("rootfs_b"),
          .slot_group = std::string("rootfs"),
          .switch_policy = std::string("confirm_after_boot"),
          .confirm_deadline = std::string("2026-04-08T14:00:00Z"),
      },
      .rollback_token = RollbackToken{
          .rollback_id = std::string("rollback-011"),
          .previous_boot_target = std::string("rootfs_a"),
          .staged_artifacts = {std::string("artifact-rootfs-011")},
          .created_at = std::string("2026-04-07T14:00:00Z"),
          .expires_at = std::string("2026-04-07T14:20:00Z"),
      },
      .policy = BootConfirmationPolicySnapshot{
          .health_blocking_components = {std::string("boot-agent")},
          .required_heartbeat_entities = {std::string("boot-agent"), std::string("ota-agent")},
          .expected_slot_versions = {std::string("app@1.2.3"), std::string("rootfs@2026.04.07")},
          .rollback_token_armed = true,
          .auto_rollback_on_failure = true,
      },
  };
}

class FakeBootControlAdapter final : public IBootControlAdapter {
 public:
  explicit FakeBootControlAdapter(std::string active_target)
      : active_target_(std::move(active_target)) {}

  [[nodiscard]] dasall::infra::ota::BootTargetQueryResult get_active_target() const override {
    return dasall::infra::ota::BootTargetQueryResult::success(active_target_);
  }

  [[nodiscard]] BootMutationResult set_next_boot(std::string_view target) override {
    return BootMutationResult::success(std::string(target), std::string("set_next_boot"));
  }

  [[nodiscard]] BootMutationResult mark_boot_success(std::string_view target) override {
    success_targets.emplace_back(target);
    return BootMutationResult::success(std::string(target), std::string("mark_boot_success"));
  }

  [[nodiscard]] BootMutationResult mark_boot_failed(std::string_view target) override {
    failed_targets.emplace_back(target);
    return BootMutationResult::success(std::string(target), std::string("mark_boot_failed"));
  }

  std::vector<std::string> success_targets;
  std::vector<std::string> failed_targets;

 private:
  std::string active_target_;
};

class FakeHealthMonitor final : public IHealthMonitor {
 public:
  HealthSnapshotResult snapshot_result = HealthSnapshotResult::success();

  HealthMonitorRegistrationResult register_probe(
      const HealthProbeRegistration& registration) override {
    static_cast<void>(registration);
    return HealthMonitorRegistrationResult::success();
  }

  HealthSnapshotResult evaluate_now() override { return snapshot_result; }

  [[nodiscard]] HealthSnapshotResult get_snapshot() const override {
    return snapshot_result;
  }

  HealthListenerSubscriptionResult subscribe(IHealthStateListener& listener) override {
    static_cast<void>(listener);
    return HealthListenerSubscriptionResult::success();
  }
};

class FakeSuccessSignalProvider final : public IBootSuccessSignalProvider {
 public:
  BootSuccessSignal signal{
      .signal_received = true,
      .self_check_ok = true,
      .detail_ref = std::string("signal://ota/confirm/success"),
      .observed_ts = 1712505600000,
  };

  [[nodiscard]] BootSuccessSignal read_success_signal(std::string_view,
                                                      std::string_view) const override {
    return signal;
  }
};

class FakeHeartbeatFreshnessSource final : public IHeartbeatFreshnessSource {
 public:
  dasall::infra::ota::HeartbeatFreshnessReport report{
      .all_fresh = true,
      .watchdog_reset_detected = false,
      .stale_entities = {},
      .detail_ref = std::string("watchdog://ota/confirm/fresh"),
  };

  [[nodiscard]] dasall::infra::ota::HeartbeatFreshnessReport evaluate_freshness(
      const std::vector<std::string>& required_entities) const override {
    static_cast<void>(required_entities);
    return report;
  }
};

class FakeVersionReportSource final : public IVersionReportSource {
 public:
  VersionReportSnapshot snapshot{
      .package_id = std::string("package-011"),
      .slot_bound_versions = {std::string("app@1.2.3"), std::string("rootfs@2026.04.07")},
      .detail_ref = std::string("version://ota/confirm/match"),
      .version = 7,
      .observed_ts = 1712505605000,
  };

  [[nodiscard]] VersionReportSnapshot read_version_report(
      std::string_view) const override {
    return snapshot;
  }
};

void test_boot_confirmation_monitor_marks_boot_success_only_after_all_confirm_gates_pass() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeHealthMonitor health_monitor;
  health_monitor.snapshot_result = HealthSnapshotResult::success(HealthSnapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = 11,
      .timestamp = 1712505604000,
  });
  FakeSuccessSignalProvider success_signal_provider;
  FakeHeartbeatFreshnessSource heartbeat_source;
  FakeVersionReportSource version_report_source;

  BootConfirmationMonitor monitor(BootConfirmationMonitor::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .health_monitor = &health_monitor,
      .success_signal_provider = &success_signal_provider,
      .heartbeat_freshness_source = &heartbeat_source,
      .version_report_source = &version_report_source,
  });

  const auto result = monitor.await_confirm(make_request());
  const auto status = monitor.get_status();

  assert_true(result.state == BootConfirmationState::Confirmed && result.is_valid(),
              "BootConfirmationMonitor should mark boot success only after self-check, health, heartbeat freshness, and version report all satisfy the frozen success criteria");
  assert_true(result.boot_mutation.has_value() &&
                  result.boot_mutation->operation == "mark_boot_success",
              "BootConfirmationMonitor should call mark_boot_success on the target slot when confirm succeeds");
  assert_equal(1, static_cast<int>(boot_control_adapter.success_targets.size()),
               "BootConfirmationMonitor should invoke mark_boot_success exactly once on the success path");
  assert_equal(0, static_cast<int>(boot_control_adapter.failed_targets.size()),
               "BootConfirmationMonitor should not invoke mark_boot_failed on the success path");
  assert_equal(1, static_cast<int>(status.confirmed_total),
               "BootConfirmationMonitor should track successful confirmations in status");
  assert_true(!status.pending_confirm && status.is_valid(),
              "BootConfirmationMonitor should clear pending_confirm after a successful confirmation");
}

void test_boot_confirmation_monitor_keeps_pending_when_health_gate_is_not_ready() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeHealthMonitor health_monitor;
  health_monitor.snapshot_result = HealthSnapshotResult::success(HealthSnapshot{
      .liveness = true,
      .readiness = false,
      .degraded = false,
      .failed_components = {},
      .version = 12,
      .timestamp = 1712505604001,
  });
  FakeSuccessSignalProvider success_signal_provider;
  FakeHeartbeatFreshnessSource heartbeat_source;
  FakeVersionReportSource version_report_source;

  BootConfirmationMonitor monitor(BootConfirmationMonitor::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .health_monitor = &health_monitor,
      .success_signal_provider = &success_signal_provider,
      .heartbeat_freshness_source = &heartbeat_source,
      .version_report_source = &version_report_source,
  });

  const auto result = monitor.await_confirm(make_request());
  const auto status = monitor.get_status();

  assert_true(result.state == BootConfirmationState::Pending && result.is_valid(),
              "BootConfirmationMonitor should keep pending_confirm open when health gate is not yet ready before confirm_deadline");
  assert_equal(0, static_cast<int>(boot_control_adapter.success_targets.size()),
               "BootConfirmationMonitor should not mark boot success while health gate is still pending");
  assert_equal(0, static_cast<int>(boot_control_adapter.failed_targets.size()),
               "BootConfirmationMonitor should not mark boot failed before confirm_deadline when only the health gate is pending");
  assert_true(status.pending_confirm && status.is_valid(),
              "BootConfirmationMonitor should expose pending_confirm status while waiting on health readiness");
}

void test_boot_confirmation_monitor_times_out_by_marking_boot_failed_and_mapping_infra_code() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeHealthMonitor health_monitor;
  FakeSuccessSignalProvider success_signal_provider;
  FakeHeartbeatFreshnessSource heartbeat_source;
  FakeVersionReportSource version_report_source;

  BootConfirmationMonitor monitor(BootConfirmationMonitor::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .health_monitor = &health_monitor,
      .success_signal_provider = &success_signal_provider,
      .heartbeat_freshness_source = &heartbeat_source,
      .version_report_source = &version_report_source,
  });

  const auto result = monitor.handle_timeout(make_request());
  const auto status = monitor.get_status();

  assert_true(result.state == BootConfirmationState::TimedOut && result.is_valid() &&
                  result.result_code == ResultCode::ProviderTimeout,
              "BootConfirmationMonitor should surface confirm timeout through the frozen INF_E_OTA_BOOT_CONFIRM_TIMEOUT mapping");
  assert_true(result.error.has_value() &&
                  result.error->details.message.find("INF_E_OTA_BOOT_CONFIRM_TIMEOUT") != std::string::npos,
              "BootConfirmationMonitor should expose the frozen timeout infra code inside the timeout failure payload");
  assert_true(result.boot_mutation.has_value() &&
                  result.boot_mutation->operation == "mark_boot_failed",
              "BootConfirmationMonitor should mark the target boot as failed when confirm_deadline expires");
  assert_equal(1, static_cast<int>(boot_control_adapter.failed_targets.size()),
               "BootConfirmationMonitor should invoke mark_boot_failed exactly once on confirm timeout");
  assert_equal(1, static_cast<int>(status.timed_out_total),
               "BootConfirmationMonitor should track timeout events in status");
  assert_true(!status.pending_confirm && status.last_error_code == ResultCode::ProviderTimeout,
              "BootConfirmationMonitor should clear pending_confirm and preserve the mapped timeout code after timeout handling");
}

void test_boot_confirmation_monitor_fails_fast_on_explicit_self_check_failure() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  FakeHealthMonitor health_monitor;
  health_monitor.snapshot_result = HealthSnapshotResult::success(HealthSnapshot{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = 13,
      .timestamp = 1712505604002,
  });
  FakeSuccessSignalProvider success_signal_provider;
  success_signal_provider.signal = BootSuccessSignal{
      .signal_received = true,
      .self_check_ok = false,
      .detail_ref = std::string("signal://ota/confirm/self-check-failed"),
      .observed_ts = 1712505603000,
  };
  FakeHeartbeatFreshnessSource heartbeat_source;
  FakeVersionReportSource version_report_source;

  BootConfirmationMonitor monitor(BootConfirmationMonitor::Dependencies{
      .boot_control_adapter = &boot_control_adapter,
      .health_monitor = &health_monitor,
      .success_signal_provider = &success_signal_provider,
      .heartbeat_freshness_source = &heartbeat_source,
      .version_report_source = &version_report_source,
  });

  const auto self_check = monitor.evaluate_self_check(make_request());
  const auto result = monitor.await_confirm(make_request());
  const auto status = monitor.get_status();

  assert_true(self_check.terminal_failure && self_check.is_valid() &&
                  self_check.result_code == ResultCode::PolicyDenied,
              "BootConfirmationMonitor should treat explicit self_check_ok=false as an immediate terminal confirm failure");
  assert_true(result.state == BootConfirmationState::Failed && result.is_valid() &&
                  result.result_code == ResultCode::PolicyDenied,
              "BootConfirmationMonitor should fail fast instead of waiting for timeout after explicit self-check failure");
  assert_equal(0, static_cast<int>(boot_control_adapter.success_targets.size()),
               "BootConfirmationMonitor should not mark boot success after explicit self-check failure");
  assert_equal(1, static_cast<int>(boot_control_adapter.failed_targets.size()),
               "BootConfirmationMonitor should mark the target boot failed immediately after explicit self-check failure");
  assert_equal(1, static_cast<int>(status.failed_total),
               "BootConfirmationMonitor should track explicit self-check failures in status");
}

}  // namespace

int main() {
  try {
    test_boot_confirmation_monitor_marks_boot_success_only_after_all_confirm_gates_pass();
    test_boot_confirmation_monitor_keeps_pending_when_health_gate_is_not_ready();
    test_boot_confirmation_monitor_times_out_by_marking_boot_failed_and_mapping_infra_code();
    test_boot_confirmation_monitor_fails_fast_on_explicit_self_check_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}