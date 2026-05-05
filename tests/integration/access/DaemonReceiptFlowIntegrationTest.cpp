#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "AsyncTaskRegistry.h"
#include "CliBinaryTestSupport.h"
#include "DaemonIntegrationHarness.h"

#ifndef DASALL_CLI_BINARY_PATH
#error "DASALL_CLI_BINARY_PATH must be defined"
#endif

namespace {

using namespace std::chrono_literals;

using dasall::access::AccessDisposition;
using dasall::access::AsyncTaskRegistry;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct CancelProbe {
  int calls = 0;
  std::string last_request_id;
  std::string last_actor_ref;
};

[[nodiscard]] DaemonAccessPipelineOptions make_async_options(
    std::shared_ptr<AsyncTaskRegistry> registry,
    std::string receipt_ref,
    CancelProbe* cancel_probe = nullptr) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_profile_id = "daemon.receipt.flow.integration";
  options.async_task_registry = std::move(registry);
  options.runtime_dispatch_backend = [receipt_ref = std::move(receipt_ref)](
                                        const RuntimeDispatchRequest&) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::AcceptedAsync;
    result.receipt_ref = receipt_ref;
    return result;
  };
  options.runtime_cancel_backend = [cancel_probe](std::string_view request_id,
                                                  std::string_view actor_ref) {
    if (cancel_probe == nullptr) {
      return false;
    }
    ++cancel_probe->calls;
    cancel_probe->last_request_id = std::string(request_id);
    cancel_probe->last_actor_ref = std::string(actor_ref);
    return true;
  };
  return options;
}

void accepted_async_status_roundtrip_covers_active_completed_and_owner_mismatch() {
  auto registry = std::make_shared<AsyncTaskRegistry>("daemon-receipt-secret", 30s);
  DaemonIntegrationHarness harness(
      make_async_options(registry, "receipt:async-active"));

  const auto accepted = harness.make_client().submit("queue async work");
  assert_true(accepted.ok(),
              "daemon receipt flow should parse accepted_async response");
  assert_true(accepted.is_accepted_async(),
              "daemon receipt flow should return accepted_async disposition");
  assert_true(accepted.receipt_ref.has_value(),
              "daemon receipt flow should return receipt_ref for async submit");
  assert_equal(std::string("receipt:async-active"), *accepted.receipt_ref,
               "daemon receipt flow should preserve runtime receipt_ref");

  const auto receipt_query = registry->query_receipt(*accepted.receipt_ref);
  assert_true(receipt_query.receipt.has_value(),
              "daemon receipt flow should register async receipt in registry");
  const auto& receipt = *receipt_query.receipt;

  const auto active = harness.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(active.ok(),
              "daemon receipt flow should parse active status response");
  assert_true(active.is_completed(),
              "daemon receipt flow status command should return completed disposition envelope");
  assert_true(active.response_text.has_value(),
              "daemon receipt flow active status should project task_status into response_text");
  assert_equal(std::string("active"), *active.response_text,
               "daemon receipt flow should report newly accepted receipt as active");
  assert_true(active.task_completed.has_value() && !*active.task_completed,
              "daemon receipt flow should keep active status non-final");

  const auto mismatch = harness.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      "local://uid/2000");
  assert_true(mismatch.ok(),
              "daemon receipt flow should parse owner mismatch rejection");
  assert_true(mismatch.error_ref.has_value(),
              "daemon receipt flow should surface status owner mismatch");
  assert_equal(std::string("status_owner_mismatch"), *mismatch.error_ref,
               "daemon receipt flow should reject cross-subject status queries");

  assert_true(registry->mark_completed(receipt.receipt_id, "completed"),
              "daemon receipt flow fixture should be able to mark receipt completed");

  const auto completed = harness.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(completed.ok(),
              "daemon receipt flow should parse completed status response");
  assert_true(completed.response_text.has_value(),
              "daemon receipt flow completed status should surface response_text");
  assert_equal(std::string("completed"), *completed.response_text,
               "daemon receipt flow should report completed receipt after registry update");
  assert_true(completed.task_completed.has_value() && *completed.task_completed,
              "daemon receipt flow should mark completed status as final");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "daemon receipt flow should stop the daemon cleanly after status checks");
}

void cancel_roundtrip_rejects_owner_mismatch_and_marks_cancelled() {
  auto registry = std::make_shared<AsyncTaskRegistry>("daemon-receipt-secret", 30s);
  CancelProbe cancel_probe;
  DaemonIntegrationHarness harness(
      make_async_options(registry, "receipt:async-cancel", &cancel_probe));

  const auto accepted = harness.make_client().submit("queue cancellable work");
  assert_true(accepted.receipt_ref.has_value(),
              "daemon cancel flow should produce receipt_ref before cancel");

  const auto receipt_query = registry->query_receipt(*accepted.receipt_ref);
  assert_true(receipt_query.receipt.has_value(),
              "daemon cancel flow should register receipt for cancel follow-up");
  const auto& receipt = *receipt_query.receipt;

  const auto rejected_cancel = harness.make_client().cancel(
      receipt.receipt_id,
      "invalid-token",
      receipt.actor_ref);
  assert_true(rejected_cancel.ok(),
              "daemon cancel flow should parse owner-mismatch rejection");
  assert_true(rejected_cancel.error_ref.has_value(),
              "daemon cancel flow should surface cancel owner mismatch");
  assert_equal(std::string("cancel_owner_mismatch"), *rejected_cancel.error_ref,
               "daemon cancel flow should fail closed on ownership mismatch");
  assert_equal(0, cancel_probe.calls,
               "daemon cancel flow should not forward mismatched cancel requests");

  const auto cancelled = harness.make_client().cancel(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(cancelled.ok(),
              "daemon cancel flow should parse successful cancel response");
  assert_true(cancelled.response_text.has_value(),
              "daemon cancel flow should surface cancelled status text");
  assert_equal(std::string("cancelled"), *cancelled.response_text,
               "daemon cancel flow should report cancelled status over wire");
  assert_equal(1, cancel_probe.calls,
               "daemon cancel flow should forward owner-matched cancel exactly once");
  assert_equal(std::string("cli-run"), cancel_probe.last_request_id,
               "daemon cancel flow should forward receipt-bound request_id to runtime cancel backend");
  assert_equal(std::string("local://uid/1000"), cancel_probe.last_actor_ref,
               "daemon cancel flow should forward authenticated actor_ref to runtime cancel backend");

  const auto cancelled_status = harness.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(cancelled_status.ok(),
              "daemon cancel flow should parse post-cancel status response");
  assert_true(cancelled_status.response_text.has_value(),
              "daemon cancel flow should surface cancelled status on status query");
  assert_equal(std::string("cancelled"), *cancelled_status.response_text,
               "daemon cancel flow should keep cancelled receipt visible to owner");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "daemon cancel flow should stop the daemon cleanly after cancel checks");
}

void expired_receipt_returns_status_expired() {
  auto registry = std::make_shared<AsyncTaskRegistry>("daemon-receipt-secret", 5ms);
  DaemonIntegrationHarness harness(
      make_async_options(registry, "receipt:async-expired"));

  const auto accepted = harness.make_client().submit("queue expiring work");
  assert_true(accepted.receipt_ref.has_value(),
              "daemon expiry flow should produce receipt_ref before expiry check");

  const auto receipt_query = registry->query_receipt(*accepted.receipt_ref);
  assert_true(receipt_query.receipt.has_value(),
              "daemon expiry flow should register receipt before expiry window elapses");
  const auto& receipt = *receipt_query.receipt;

  std::this_thread::sleep_for(8ms);

  const auto expired = harness.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(expired.ok(),
              "daemon expiry flow should parse expired status rejection");
  assert_true(expired.error_ref.has_value(),
              "daemon expiry flow should surface status_expired error_ref");
  assert_equal(std::string("status_expired"), *expired.error_ref,
               "daemon expiry flow should expose status_expired after TTL elapses");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "daemon expiry flow should stop the daemon cleanly after expiry checks");
}

  void accepted_async_binary_json_preserves_explicit_ids_and_stdout_routing() {
    auto registry = std::make_shared<AsyncTaskRegistry>("daemon-receipt-secret", 30s);
    DaemonIntegrationHarness harness(
      make_async_options(registry, "receipt:async-binary"));

    const auto accepted = dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_CLI_BINARY_PATH,
        "--socket-path",
        harness.socket_path(),
        "run",
        "{\"prompt\":\"queue async work\"}",
        "--async",
        "--json",
        "--request-id",
        "cli-013-async",
        "--trace-id",
        "cli-013-async-trace",
      },
      std::filesystem::current_path());
    assert_equal(0,
           accepted.exit_code,
           "daemon receipt flow should keep accepted_async CLI binary path on exit 0; stdout=" +
             accepted.stdout_text + " stderr=" + accepted.stderr_text);
    assert_true(accepted.stderr_text.empty(),
          "daemon receipt flow should keep accepted_async JSON output off stderr; stderr=" +
            accepted.stderr_text);
    assert_true(accepted.stdout_text.find("\"disposition\":\"accepted_async\"") !=
            std::string::npos,
          "daemon receipt flow should preserve accepted_async disposition in built CLI output; stdout=" +
            accepted.stdout_text);
    assert_true(accepted.stdout_text.find("\"request_id\":\"cli-013-async\"") !=
            std::string::npos,
          "daemon receipt flow should preserve explicit request_id in built CLI output; stdout=" +
            accepted.stdout_text);
    assert_true(accepted.stdout_text.find("\"trace_id\":\"cli-013-async-trace\"") !=
            std::string::npos,
          "daemon receipt flow should preserve explicit trace_id in built CLI output; stdout=" +
            accepted.stdout_text);

    const auto receipt_ref =
      dasall::tests::integration::access_support::extract_json_string_field(
        accepted.stdout_text,
        "receipt_ref");
    assert_true(receipt_ref.has_value() && !receipt_ref->empty(),
          "daemon receipt flow should return receipt_ref in built CLI accepted_async JSON output; stdout=" +
            accepted.stdout_text);

    const auto receipt_query = registry->query_receipt(*receipt_ref);
    assert_true(receipt_query.receipt.has_value(),
          "daemon receipt flow should register built CLI accepted_async receipts in registry");
    const auto& receipt = *receipt_query.receipt;
    assert_equal(std::string("cli-013-async"),
           receipt.request_id,
           "daemon receipt flow should preserve explicit request_id in async receipt storage");

    const auto status = dasall::tests::integration::access_support::run_process_capture_split(
      {
        DASALL_CLI_BINARY_PATH,
        "--socket-path",
        harness.socket_path(),
        "status",
        receipt.receipt_id,
        receipt.ownership_token,
        receipt.actor_ref,
        "--json",
      },
      std::filesystem::current_path());
    assert_equal(0,
           status.exit_code,
           "daemon receipt flow should let built CLI query status for accepted receipts; stdout=" +
             status.stdout_text + " stderr=" + status.stderr_text);
    assert_true(status.stderr_text.empty(),
          "daemon receipt flow should keep successful status JSON output off stderr; stderr=" +
            status.stderr_text);
    assert_true(status.stdout_text.find("\"disposition\":\"completed\"") !=
            std::string::npos,
          "daemon receipt flow should keep completed envelope for status query output; stdout=" +
            status.stdout_text);
    assert_true(status.stdout_text.find("\"response_text\":\"active\"") !=
            std::string::npos,
          "daemon receipt flow should surface active task status in built CLI JSON output; stdout=" +
            status.stdout_text);

    harness.stop();
    assert_true(harness.daemon_stopped_cleanly(),
          "daemon receipt flow should stop the daemon cleanly after built CLI async checks");
  }

}  // namespace

int main() {
  try {
    accepted_async_status_roundtrip_covers_active_completed_and_owner_mismatch();
    cancel_roundtrip_rejects_owner_mismatch_and_marks_cancelled();
    expired_receipt_returns_status_expired();
    accepted_async_binary_json_preserves_explicit_ids_and_stdout_routing();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonReceiptFlowIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}