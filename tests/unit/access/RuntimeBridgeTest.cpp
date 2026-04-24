#include <exception>
#include <iostream>
#include <string>

#include "RuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_runtime_ready_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-020-sync";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "run";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.decision = "Allow";
  request.request_context["normalizer_ready"] = "true";
  request.request_context["request_id"] = "req-020-sync";
  request.request_context["session_id"] = "sess-020-sync";
  request.request_context["trace_id"] = "trace-020-sync";
  return request;
}

void dispatch_maps_sync_result_and_preserves_trace_context() {
  using dasall::access::AccessDisposition;
  using dasall::access::RuntimeBridge;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeBridge bridge(
      [](const auto&) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        result.publish_envelope = dasall::access::PublishEnvelope{};
        result.publish_envelope->protocol_status_hint = "200 OK";
        return result;
      },
      {});

  const auto mapped = bridge.dispatch(make_runtime_ready_request());

  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(mapped.disposition),
               "sync path should keep completed disposition");
  assert_true(mapped.response_context.contains("request_id"),
              "runtime bridge should preserve request_id in response context");
  assert_equal(std::string("trace-020-sync"),
               mapped.response_context.at("trace_id"),
               "runtime bridge should preserve trace_id in response context");
}

}  // namespace

int main() {
  try {
    dispatch_maps_sync_result_and_preserves_trace_context();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
