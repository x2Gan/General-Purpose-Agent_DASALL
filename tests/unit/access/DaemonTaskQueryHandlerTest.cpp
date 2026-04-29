#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "AsyncTaskRegistry.h"
#include "daemon/DaemonTaskQueryHandler.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-daemon-status";
  request.packet.entry_type = "daemon";
  request.packet.protocol_kind = "ipc_uds";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["request_id"] = "req-daemon-status";
  request.request_context["session_id"] = "sess-daemon-status";
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_async_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
  result.receipt_ref = "receipt:daemon-status";
  return result;
}

void status_found_and_active() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::daemon::DaemonTaskOwner;
  using dasall::access::daemon::DaemonTaskQueryHandler;
  using dasall::access::daemon::DaemonTaskQueryStatus;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("daemon-status-secret");
  DaemonTaskQueryHandler handler(registry);

  const auto receipt = registry.register_async_accept(make_request(), make_async_result());
  assert_true(receipt.has_value(), "accepted_async should create receipt");

  const auto result = handler.handle_status(
      receipt->receipt_id,
      DaemonTaskOwner{
          .actor_ref = receipt->actor_ref,
          .ownership_token = receipt->ownership_token,
      });

  assert_equal(static_cast<int>(DaemonTaskQueryStatus::Active),
               static_cast<int>(result.status),
               "owner query should return active status");
  assert_equal(std::string("active"), result.task_status,
               "owner query should map pending receipt to active");
}

void status_not_found() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::daemon::DaemonTaskOwner;
  using dasall::access::daemon::DaemonTaskQueryHandler;
  using dasall::access::daemon::DaemonTaskQueryStatus;
  using dasall::tests::support::assert_equal;

  AsyncTaskRegistry registry("daemon-status-secret");
  DaemonTaskQueryHandler handler(registry);

  const auto result = handler.handle_status(
      "receipt:not-found",
      DaemonTaskOwner{
          .actor_ref = "user://tenant-a/alice",
          .ownership_token = "invalid",
      });

  assert_equal(static_cast<int>(DaemonTaskQueryStatus::Missing),
               static_cast<int>(result.status),
               "missing receipt should return Missing status");
  assert_equal(std::string("missing"), result.task_status,
               "missing receipt should return missing task_status");
}

void status_expired() {
  using namespace std::chrono_literals;
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::daemon::DaemonTaskOwner;
  using dasall::access::daemon::DaemonTaskQueryHandler;
  using dasall::access::daemon::DaemonTaskQueryStatus;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("daemon-status-secret", std::chrono::milliseconds(5));
  DaemonTaskQueryHandler handler(registry);

  const auto receipt = registry.register_async_accept(make_request(), make_async_result());
  assert_true(receipt.has_value(), "accepted_async should create receipt");

  std::this_thread::sleep_for(8ms);

  const auto result = handler.handle_status(
      receipt->receipt_id,
      DaemonTaskOwner{
          .actor_ref = receipt->actor_ref,
          .ownership_token = receipt->ownership_token,
      });

  assert_equal(static_cast<int>(DaemonTaskQueryStatus::Expired),
               static_cast<int>(result.status),
               "expired receipt should return Expired status");
  assert_equal(std::string("expired"), result.task_status,
               "expired receipt should return expired task_status");
}

void status_owner_mismatch() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::daemon::DaemonTaskOwner;
  using dasall::access::daemon::DaemonTaskQueryHandler;
  using dasall::access::daemon::DaemonTaskQueryStatus;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("daemon-status-secret");
  DaemonTaskQueryHandler handler(registry);

  const auto receipt = registry.register_async_accept(make_request(), make_async_result());
  assert_true(receipt.has_value(), "accepted_async should create receipt");

  const auto result = handler.handle_status(
      receipt->receipt_id,
      DaemonTaskOwner{
          .actor_ref = "user://tenant-b/bob",
          .ownership_token = receipt->ownership_token,
      });

  assert_equal(static_cast<int>(DaemonTaskQueryStatus::OwnerMismatch),
               static_cast<int>(result.status),
               "owner mismatch should return OwnerMismatch status");
  assert_equal(std::string("mismatch"), result.task_status,
               "owner mismatch should return mismatch task_status");
}

}  // namespace

int main() {
  try {
    status_found_and_active();
    status_not_found();
    status_expired();
    status_owner_mismatch();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
