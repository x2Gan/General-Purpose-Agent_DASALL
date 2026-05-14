#include <exception>
#include <iostream>
#include <optional>
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

    void validate_ownership_across_secret_rotation_window() {
      using dasall::access::AsyncTaskRegistry;
      using dasall::tests::support::assert_true;

      AsyncTaskRegistry registry(
        AsyncTaskRegistry::OwnershipTokenKey{
          .key_id = "key-v1",
          .secret = "static-secret-v1",
        });
      const auto first_receipt = registry.register_async_accept(make_request(), make_async_result());

      assert_true(first_receipt.has_value(), "rotation test should register the first receipt");
      assert_true(
        registry.validate_ownership(first_receipt->receipt_id,
                      "user://tenant-a/alice",
                      first_receipt->ownership_token),
        "first token should validate before rotation");

      registry.rotate_ownership_keys(
        AsyncTaskRegistry::OwnershipTokenKey{
          .key_id = "key-v2",
          .secret = "static-secret-v2",
        },
        AsyncTaskRegistry::OwnershipTokenKey{
          .key_id = "key-v1",
          .secret = "static-secret-v1",
        });

      assert_true(
        registry.validate_ownership(first_receipt->receipt_id,
                      "user://tenant-a/alice",
                      first_receipt->ownership_token),
        "current+previous key window should keep validating previously issued tokens");

      auto rotated_request = make_request();
      rotated_request.packet.packet_id = "pkt-022-owner-rotated";
      rotated_request.request_context["request_id"] = "req-022-owner-rotated";
      rotated_request.request_context["session_id"] = "sess-022-owner-rotated";
      const auto second_receipt = registry.register_async_accept(rotated_request, make_async_result());
      assert_true(second_receipt.has_value(), "rotation test should register the second receipt");
      assert_true(second_receipt->ownership_token != first_receipt->ownership_token,
            "rotated current key should produce a different ownership token");

      registry.rotate_ownership_keys(
        AsyncTaskRegistry::OwnershipTokenKey{
          .key_id = "key-v2",
          .secret = "static-secret-v2",
        },
        std::nullopt);

      assert_true(
        !registry.validate_ownership(first_receipt->receipt_id,
                       "user://tenant-a/alice",
                       first_receipt->ownership_token),
        "dropping the previous key should invalidate the old token");
      assert_true(
        registry.validate_ownership(second_receipt->receipt_id,
                      "user://tenant-a/alice",
                      second_receipt->ownership_token),
        "latest token should stay valid under the new current key");
    }

}  // namespace

int main() {
  try {
    validate_ownership_with_actor_and_token();
      validate_ownership_across_secret_rotation_window();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
