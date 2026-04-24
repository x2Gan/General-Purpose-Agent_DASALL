#include <exception>
#include <iostream>
#include <string>

#include "AsyncTaskRegistry.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-022-owner";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["request_id"] = "req-022-owner";
  request.request_context["session_id"] = "sess-022-owner";
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_async_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
  result.receipt_ref = "receipt:022-owner";
  return result;
}

void validate_ownership_with_actor_and_token() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("static-secret-022");
  const auto receipt = registry.register_async_accept(make_request(), make_async_result());

  assert_true(receipt.has_value(), "receipt should be registered");
  assert_true(
      registry.validate_ownership(
          receipt->receipt_id,
          "user://tenant-a/alice",
          receipt->ownership_token),
      "owner with correct token should pass ownership validation");

  assert_true(
      !registry.validate_ownership(
          receipt->receipt_id,
          "user://tenant-a/bob",
          receipt->ownership_token),
      "different actor must be rejected");

  assert_true(
      !registry.validate_ownership(
          receipt->receipt_id,
          "user://tenant-a/alice",
          "deadbeef"),
      "owner with wrong token must be rejected");
}

}  // namespace

int main() {
  try {
    validate_ownership_with_actor_and_token();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
