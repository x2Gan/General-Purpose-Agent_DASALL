#include <exception>
#include <iostream>
#include <string>

#include "RuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_async_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-020-async";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "run_async";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.decision = "Allow";
  request.async_allowed = true;
  request.request_context["normalizer_ready"] = "true";
  request.request_context["request_id"] = "req-020-async";
  request.request_context["session_id"] = "sess-020-async";
  request.request_context["trace_id"] = "trace-020-async";
  return request;
}

void dispatch_generates_fallback_receipt_for_async_accept() {
  using dasall::access::AccessDisposition;
  using dasall::access::RuntimeBridge;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeBridge bridge(
      [](const auto&) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::AcceptedAsync;
        // 模拟 runtime 返回 accepted_async 但未携带 receipt 的场景。
        return result;
      },
      {});

  const auto mapped = bridge.dispatch(make_async_request());

  assert_equal(static_cast<int>(AccessDisposition::AcceptedAsync),
               static_cast<int>(mapped.disposition),
               "async path should keep accepted-async disposition");
  assert_true(mapped.receipt_ref.has_value(),
              "runtime bridge should generate fallback receipt when backend omits it");
  assert_true(mapped.receipt_ref->find("receipt:req-020-async") != std::string::npos,
              "fallback receipt should contain normalized request_id");
}

}  // namespace

int main() {
  try {
    dispatch_generates_fallback_receipt_for_async_accept();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
