#include <exception>
#include <iostream>
#include <string>

#include "AsyncTaskRegistry.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-022-async";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["request_id"] = "req-022-async";
  request.request_context["session_id"] = "sess-022-async";
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_async_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
  result.receipt_ref = "receipt:022-async";
  return result;
}

void register_async_accept_and_query_receipt() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("static-secret-022");
  const auto receipt = registry.register_async_accept(make_request(), make_async_result());

  assert_true(receipt.has_value(), "accepted async should register receipt");
  assert_equal(std::string("receipt:022-async"), receipt->receipt_id,
               "registry should preserve receipt ref from runtime");

  const auto queried = registry.query_receipt(receipt->receipt_id);
  assert_equal(static_cast<int>(AsyncTaskRegistry::QueryStatus::Found),
               static_cast<int>(queried.status),
               "query should find registered receipt");
  assert_true(queried.receipt.has_value(), "found receipt should carry payload");
  assert_equal(std::string("user://tenant-a/alice"), queried.receipt->actor_ref,
               "receipt should preserve actor_ref for later ownership check");
}

}  // namespace

int main() {
  try {
    register_async_accept_and_query_receipt();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
