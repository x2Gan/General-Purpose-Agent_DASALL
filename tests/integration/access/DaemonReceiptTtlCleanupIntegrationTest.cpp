#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "AsyncTaskRegistry.h"
#include "DaemonInProcessFixture.h"
#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

using namespace std::chrono_literals;

using dasall::access::AccessDisposition;
using dasall::access::AsyncTaskRegistry;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::access::daemon::DaemonAsyncPreference;
using dasall::access::daemon::UdsRequestFrame;
using dasall::tests::integration::access_support::DaemonInProcessFixture;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] DaemonAccessPipelineOptions make_ttl_options(
    const std::shared_ptr<AsyncTaskRegistry>& registry) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_profile_id = "daemon.receipt.ttl.cleanup";
  options.async_task_registry = registry;
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest& request) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::AcceptedAsync;
    result.receipt_ref = "receipt:" + request.packet.payload;
    return result;
  };
  return options;
}

[[nodiscard]] dasall::apps::cli::DaemonClientResponse submit_unique_async_request(
    const DaemonInProcessFixture& fixture,
    const std::string& request_id,
    const std::string& payload) {
  UdsRequestFrame frame;
  frame.request_id = request_id;
  frame.trace_id = request_id + "-trace";
  frame.session_hint = std::nullopt;
  frame.idempotency_key = request_id + "-idem";
  frame.command = "run";
  frame.args = {};
  frame.payload = payload;
  frame.async_preference = DaemonAsyncPreference::PreferAsync;
  return fixture.send_frame(frame);
}

void expired_receipts_are_cleaned_up_and_count_returns_to_baseline() {
  auto registry = std::make_shared<AsyncTaskRegistry>(
      "daemon-receipt-ttl-cleanup",
      5ms);
  DaemonInProcessFixture fixture(make_ttl_options(registry));

  const auto first_submit = submit_unique_async_request(fixture, "ttl-first", "ttl-first");
  const auto second_submit = submit_unique_async_request(fixture, "ttl-second", "ttl-second");
  assert_true(first_submit.receipt_ref.has_value() && second_submit.receipt_ref.has_value(),
              "daemon receipt ttl cleanup gate should produce receipt_ref values before expiry");

  const auto first_query = registry->query_receipt(*first_submit.receipt_ref);
  const auto second_query = registry->query_receipt(*second_submit.receipt_ref);
  assert_true(first_query.receipt.has_value() && second_query.receipt.has_value(),
              "daemon receipt ttl cleanup gate should register both async receipts before expiry");
  assert_equal(static_cast<std::size_t>(2U), fixture.receipt_active_count(),
               "daemon receipt ttl cleanup gate should expose both active receipts before ttl elapses");

  std::this_thread::sleep_for(8ms);

  const auto expired = fixture.make_client().query_status(
      first_query.receipt->receipt_id,
      first_query.receipt->ownership_token,
      first_query.receipt->actor_ref);
  assert_true(expired.ok(),
              "daemon receipt ttl cleanup gate should parse expired status rejection");
  assert_true(expired.error_ref.has_value(),
              "daemon receipt ttl cleanup gate should surface status_expired after ttl elapses");
  assert_equal(std::string("status_expired"), *expired.error_ref,
               "daemon receipt ttl cleanup gate should expose status_expired for expired receipts");

  assert_equal(static_cast<std::size_t>(1U), fixture.prune_expired_receipts(),
               "daemon receipt ttl cleanup gate should prune the remaining expired receipt deterministically");
  assert_equal(static_cast<std::size_t>(0U), fixture.receipt_active_count(),
               "daemon receipt ttl cleanup gate should return receipt_active_count to baseline after cleanup");
  assert_equal(static_cast<std::size_t>(0U), fixture.active_connection_count(),
               "daemon receipt ttl cleanup gate should leave no active connections after expiry checks");

  fixture.stop();
  assert_true(fixture.daemon_stopped_cleanly(),
              "daemon receipt ttl cleanup fixture should stop cleanly after ttl cleanup validation");
}

}  // namespace

int main() {
  try {
    expired_receipts_are_cleaned_up_and_count_returns_to_baseline();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonReceiptTtlCleanupIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}