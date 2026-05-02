#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "DaemonIntegrationHarness.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::access::daemon::UdsRequestFrame;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct RuntimeProbe {
  int calls = 0;
};

[[nodiscard]] DaemonAccessPipelineOptions make_base_options(RuntimeProbe* probe) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 4096;
  options.daemon_profile_id = "daemon.reject.integration";
  options.runtime_dispatch_backend = [probe](const RuntimeDispatchRequest&) {
    ++probe->calls;
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };
  return options;
}

void unknown_command_is_rejected_before_runtime_dispatch() {
  RuntimeProbe probe;
  auto options = make_base_options(&probe);

  DaemonIntegrationHarness harness(std::move(options));

  UdsRequestFrame frame;
  frame.request_id = "unknown_command";
  frame.trace_id = "unknown_command-trace";
  frame.command = "run";

  const auto response = harness.send_frame(frame);
  assert_true(response.ok(),
              "unknown-command integration should receive a parsed daemon rejection");
  assert_true(!response.is_completed() && !response.is_accepted_async(),
              "unknown-command integration should reject unsupported commands");
  assert_true(response.error_ref.has_value(),
              "unknown-command integration should surface a stable error_ref");
  assert_equal(std::string("unknown_command"), *response.error_ref,
               "unknown-command integration should map to unknown_command");
  assert_equal(0, probe.calls,
               "unknown-command integration should not reach runtime dispatch");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "unknown-command integration should stop the daemon cleanly");
}

void auth_deny_is_rejected_before_runtime_dispatch() {
  RuntimeProbe probe;
  auto options = make_base_options(&probe);
  options.auth_view.trusted_local_subjects.clear();

  DaemonIntegrationHarness harness(std::move(options));

  const auto response = harness.make_client().submit("auth should fail");
  assert_true(response.ok(),
              "auth-deny integration should receive a parsed daemon rejection");
  assert_true(response.error_ref.has_value(),
              "auth-deny integration should surface authentication failure");
  assert_equal(std::string("authentication_required"), *response.error_ref,
               "auth-deny integration should map unresolved local subject to authentication_required");
  assert_equal(0, probe.calls,
               "auth-deny integration should not reach runtime dispatch");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "auth-deny integration should stop the daemon cleanly");
}

void validation_reject_is_rejected_before_runtime_dispatch() {
  RuntimeProbe probe;
  auto options = make_base_options(&probe);
  options.publish_view.max_payload_bytes = 4;

  DaemonIntegrationHarness harness(std::move(options));

  const auto response = harness.make_client().submit("payload too large");
  assert_true(response.ok(),
              "validation-reject integration should receive a parsed daemon rejection");
  assert_true(response.error_ref.has_value(),
              "validation-reject integration should surface payload validation failure");
  assert_equal(std::string("payload_size_limit_exceeded"), *response.error_ref,
               "validation-reject integration should preserve validator error_ref");
  assert_equal(0, probe.calls,
               "validation-reject integration should not reach runtime dispatch");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "validation-reject integration should stop the daemon cleanly");
}

void runtime_bridge_unavailable_rejects_after_access_validation() {
  RuntimeProbe probe;
  auto options = make_base_options(&probe);
  options.runtime_dispatch_backend = {};

  DaemonIntegrationHarness harness(std::move(options));

  const auto response = harness.make_client().submit("runtime backend unavailable");
  assert_true(response.ok(),
              "runtime-bridge-unavailable integration should receive a parsed daemon rejection");
  assert_true(response.error_ref.has_value(),
              "runtime-bridge-unavailable integration should surface bridge rejection reason");
  assert_equal(std::string("runtime bridge backend is not configured"),
               *response.error_ref,
               "runtime-bridge-unavailable integration should preserve RuntimeBridge reject reason");
  assert_equal(0, probe.calls,
               "runtime-bridge-unavailable integration should fail before a runtime backend call exists");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "runtime-bridge-unavailable integration should stop the daemon cleanly");
}

}  // namespace

int main() {
  try {
    unknown_command_is_rejected_before_runtime_dispatch();
    auth_deny_is_rejected_before_runtime_dispatch();
    validation_reject_is_rejected_before_runtime_dispatch();
    runtime_bridge_unavailable_rejects_after_access_validation();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonRejectPathIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}