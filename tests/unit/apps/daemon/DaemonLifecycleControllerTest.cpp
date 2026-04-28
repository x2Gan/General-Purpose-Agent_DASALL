#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "DaemonLifecycleController.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonLifecycleController;
using dasall::apps::daemon::DaemonLifecycleObservation;
using dasall::apps::daemon::DaemonLifecycleState;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_lifecycle_accepts_valid_state_progression() {
  DaemonLifecycleController controller;

  assert_true(controller.start(), "start should move controller into Bootstrapping");
  assert_equal(static_cast<int>(DaemonLifecycleState::Bootstrapping),
               static_cast<int>(controller.state()),
               "start should leave controller in Bootstrapping");
  assert_equal(static_cast<int>(DaemonLifecycleObservation::Starting),
               static_cast<int>(controller.observation()),
               "Bootstrapping should surface STARTING observation");
  assert_true(!controller.allows_new_requests(),
              "Bootstrapping should reject new requests");

  assert_true(controller.mark_binding(),
              "mark_binding should move controller into Binding");
  assert_equal(static_cast<int>(DaemonLifecycleState::Binding),
               static_cast<int>(controller.state()),
               "mark_binding should leave controller in Binding");

  assert_true(controller.mark_ready(), "mark_ready should move controller into Ready");
  assert_equal(static_cast<int>(DaemonLifecycleState::Ready),
               static_cast<int>(controller.state()),
               "mark_ready should leave controller in Ready");
  assert_equal(static_cast<int>(DaemonLifecycleObservation::Ready),
               static_cast<int>(controller.observation()),
               "Ready should surface READY observation");
  assert_true(controller.allows_new_requests(), "Ready should accept new requests");
}

void test_lifecycle_rejects_illegal_transitions() {
  DaemonLifecycleController controller;

  assert_true(!controller.mark_binding(),
              "mark_binding should reject transitions before start");
  assert_true(!controller.mark_ready(),
              "mark_ready should reject transitions before Binding");
  assert_true(controller.start(), "start should succeed from Stopped");
  assert_true(!controller.start(), "start should reject duplicate transition");
  assert_true(controller.mark_binding(), "mark_binding should succeed after start");
  assert_true(!controller.mark_binding(),
              "mark_binding should reject duplicate Binding transition");
}

void test_shutdown_enters_draining_and_rejects_new_requests() {
  using namespace std::chrono_literals;

  DaemonLifecycleController controller;
  assert_true(controller.start(), "start should succeed before shutdown test");
  assert_true(controller.mark_binding(), "mark_binding should succeed before shutdown test");
  assert_true(controller.mark_ready(), "mark_ready should succeed before shutdown test");
  assert_true(controller.begin_request(), "Ready should admit one inflight request");

  dasall::apps::daemon::DaemonShutdownResult shutdown_result;
  std::thread shutdown_thread([&controller, &shutdown_result]() {
    shutdown_result = controller.shutdown(std::chrono::milliseconds(250));
  });

  for (int attempt = 0; attempt < 50; ++attempt) {
    if (controller.state() == DaemonLifecycleState::Draining) {
      break;
    }

    std::this_thread::sleep_for(5ms);
  }

  assert_equal(static_cast<int>(DaemonLifecycleState::Draining),
               static_cast<int>(controller.state()),
               "shutdown should enter Draining before inflight is released");
  assert_equal(static_cast<int>(DaemonLifecycleObservation::Stopping),
               static_cast<int>(controller.observation()),
               "Draining should surface STOPPING observation");
  assert_true(!controller.allows_new_requests(),
              "Draining should reject new requests");
  assert_true(!controller.begin_request(),
              "Draining should refuse new inflight requests");

  controller.finish_request();
  shutdown_thread.join();

  assert_true(shutdown_result.drained,
              "shutdown should report drained after inflight request finishes");
  assert_true(!shutdown_result.timed_out,
              "shutdown should not time out when inflight request drains");
  assert_equal(static_cast<int>(DaemonLifecycleState::Stopped),
               static_cast<int>(controller.state()),
               "shutdown should leave controller in Stopped after draining");
}

void test_shutdown_reports_timeout_when_inflight_never_drains() {
  using namespace std::chrono_literals;

  DaemonLifecycleController controller;
  assert_true(controller.start(), "start should succeed before timeout test");
  assert_true(controller.mark_binding(),
              "mark_binding should succeed before timeout test");
  assert_true(controller.mark_ready(), "mark_ready should succeed before timeout test");
  assert_true(controller.begin_request(), "Ready should admit one inflight request");

  const auto shutdown_result = controller.shutdown(1ms);
  assert_true(!shutdown_result.drained,
              "shutdown should report undrained state when timeout is reached");
  assert_true(shutdown_result.timed_out,
              "shutdown should report timeout when inflight never drains");
  assert_equal(1U, shutdown_result.abandoned_requests,
               "shutdown timeout should surface abandoned inflight count");
  assert_equal(static_cast<int>(DaemonLifecycleState::Stopped),
               static_cast<int>(controller.state()),
               "shutdown timeout should still leave controller in Stopped");
  assert_true(!controller.begin_request(),
              "Stopped controller should reject new requests after timeout shutdown");
}

void test_failed_state_surfaces_not_ready_observation() {
  DaemonLifecycleController controller;

  assert_true(controller.start(), "start should succeed before failed-state test");
  assert_true(controller.mark_binding(),
              "mark_binding should succeed before failed-state test");
  assert_true(controller.mark_failed(),
              "mark_failed should move controller into Failed");
  assert_equal(static_cast<int>(DaemonLifecycleState::Failed),
               static_cast<int>(controller.state()),
               "mark_failed should leave controller in Failed");
  assert_equal(static_cast<int>(DaemonLifecycleObservation::NotReady),
               static_cast<int>(controller.observation()),
               "Failed should surface NOT_READY observation");
  assert_true(!controller.allows_new_requests(),
              "Failed should reject all new requests");
}

}  // namespace

int main() {
  try {
    test_lifecycle_accepts_valid_state_progression();
    test_lifecycle_rejects_illegal_transitions();
    test_shutdown_enters_draining_and_rejects_new_requests();
    test_shutdown_reports_timeout_when_inflight_never_drains();
    test_failed_state_surfaces_not_ready_observation();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonLifecycleControllerTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}