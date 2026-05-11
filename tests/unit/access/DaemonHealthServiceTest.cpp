#include <exception>
#include <iostream>
#include <string>

#include "daemon/DaemonHealthService.h"
#include "support/TestAssertions.h"

namespace {

void test_snapshot_reports_ready_when_all_dependencies_ready() {
  using dasall::access::daemon::DaemonHealthInput;
  using dasall::access::daemon::DaemonHealthService;
  using dasall::access::daemon::DaemonReadinessState;
  using dasall::tests::support::assert_equal;

  const DaemonHealthService service("v1.0.0", "1", "daemon.test");

  DaemonHealthInput input;
  input.lifecycle_ready = true;
  input.listener_ready = true;
  input.gateway_ready = true;
  input.bridge_reachable = true;
  input.diagnostics_enabled = false;

  const auto snapshot = service.snapshot(input, "req-018-ready");

  assert_equal(static_cast<int>(DaemonReadinessState::Ready),
               static_cast<int>(snapshot.readiness.state),
               "readiness should be READY when lifecycle/listener/gateway/bridge are ready");
  assert_equal(std::string("READY"),
               snapshot.ping.readiness_summary,
               "ping summary should expose READY status text");
  assert_equal(std::string("default-ready"),
               snapshot.readiness.runtime_readiness_label,
               "readiness snapshot should default to default-ready runtime label");
}

void test_snapshot_reports_not_ready_when_bridge_unreachable() {
  using dasall::access::daemon::DaemonHealthInput;
  using dasall::access::daemon::DaemonHealthService;
  using dasall::access::daemon::DaemonReadinessState;
  using dasall::tests::support::assert_equal;

  const DaemonHealthService service("v1.0.0", "1", "daemon.test");

  DaemonHealthInput input;
  input.lifecycle_ready = true;
  input.listener_ready = true;
  input.gateway_ready = true;
  input.bridge_reachable = false;

  const auto snapshot = service.snapshot(input, "req-018-not-ready");

  assert_equal(static_cast<int>(DaemonReadinessState::NotReady),
               static_cast<int>(snapshot.readiness.state),
               "readiness should be NOT_READY when runtime bridge is unreachable");
  assert_equal(std::string("NOT_READY"),
               snapshot.ping.readiness_summary,
               "ping summary should expose NOT_READY status text");
}

void test_snapshot_reports_degraded_when_reasons_present() {
  using dasall::access::daemon::DaemonHealthInput;
  using dasall::access::daemon::DaemonHealthService;
  using dasall::access::daemon::DaemonReadinessState;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DaemonHealthService service("v1.0.0", "1", "daemon.profile.canary");

  DaemonHealthInput input;
  input.lifecycle_ready = true;
  input.listener_ready = true;
  input.gateway_ready = true;
  input.bridge_reachable = true;
  input.diagnostics_enabled = true;
  input.runtime_readiness_label = "degraded-ready";
  input.degraded_reasons = {"metrics_buffer_high"};

  const auto snapshot = service.snapshot(input, "req-018-degraded");

  assert_equal(static_cast<int>(DaemonReadinessState::Degraded),
               static_cast<int>(snapshot.readiness.state),
               "readiness should be DEGRADED when degraded reasons exist");
  assert_equal(std::string("DEGRADED"),
               snapshot.ping.readiness_summary,
               "ping summary should expose DEGRADED status text");
  assert_equal(std::string("daemon.profile.canary"),
               snapshot.ping.profile_id,
               "ping summary should keep profile id for operations visibility");
  assert_true(snapshot.readiness.degraded_reasons.size() == 1U,
              "degraded reasons should be preserved in readiness snapshot");
  assert_equal(std::string("degraded-ready"),
               snapshot.readiness.runtime_readiness_label,
               "degraded snapshot should preserve runtime readiness label");
}

}  // namespace

int main() {
  try {
    test_snapshot_reports_ready_when_all_dependencies_ready();
    test_snapshot_reports_not_ready_when_bridge_unreachable();
    test_snapshot_reports_degraded_when_reasons_present();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
