#include <exception>
#include <iostream>
#include <string>

#include "RuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_reject_candidate_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-020-reject";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.decision = "Allow";
  request.request_context["request_id"] = "req-020-reject";
  return request;
}

void rejects_when_normalizer_gate_is_missing() {
  using dasall::access::AccessDisposition;
  using dasall::access::RuntimeBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeBridge bridge(
      [](const auto&) {
        return dasall::access::RuntimeDispatchResult{};
      },
      {});

  const auto result = bridge.dispatch(make_reject_candidate_request());

  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "missing normalizer gate should be rejected fail-closed");
  assert_true(result.error_ref.has_value(),
              "reject mapping should carry explicit error_ref reason");
  assert_equal(std::string("100"),
               result.response_context.at("error_code"),
               "missing normalizer gate should map to ValidationRejected");
}

void cancel_is_forward_only_and_requires_non_empty_identity() {
  using dasall::access::RuntimeBridge;
  using dasall::tests::support::assert_true;

  bool cancel_called = false;
  RuntimeBridge bridge(
      {},
      [&cancel_called](std::string_view request_id, std::string_view actor_ref) {
        cancel_called = true;
        return request_id == "req-020-cancel" && actor_ref == "user://tenant-a/alice";
      });

  const bool accepted = bridge.cancel("req-020-cancel", "user://tenant-a/alice");
  const bool rejected = bridge.cancel("", "user://tenant-a/alice");

  assert_true(accepted, "cancel backend should be called for valid identities");
  assert_true(cancel_called, "cancel should be forwarded to backend");
  assert_true(!rejected, "empty cancel identity should be rejected before forwarding");
}

}  // namespace

int main() {
  try {
    rejects_when_normalizer_gate_is_missing();
    cancel_is_forward_only_and_requires_non_empty_identity();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
