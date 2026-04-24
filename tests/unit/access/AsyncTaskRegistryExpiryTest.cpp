#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "AsyncTaskRegistry.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-022-expiry";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["request_id"] = "req-022-expiry";
  request.request_context["session_id"] = "sess-022-expiry";
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_async_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
  result.receipt_ref = "receipt:022-expiry";
  return result;
}

void receipt_expires_after_ttl_window() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("static-secret-022", std::chrono::milliseconds(1));
  const auto receipt = registry.register_async_accept(make_request(), make_async_result());

  assert_true(receipt.has_value(), "receipt should be registered before expiry");

  std::this_thread::sleep_for(std::chrono::milliseconds(2));

  const auto queried = registry.query_receipt(receipt->receipt_id);
  assert_equal(static_cast<int>(AsyncTaskRegistry::QueryStatus::Expired),
               static_cast<int>(queried.status),
               "query should report expired after ttl elapsed");
  assert_true(
      !registry.validate_ownership(
          receipt->receipt_id,
          "user://tenant-a/alice",
          receipt->ownership_token),
      "expired receipt must fail ownership validation");
}

}  // namespace

int main() {
  try {
    receipt_expires_after_ttl_window();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
