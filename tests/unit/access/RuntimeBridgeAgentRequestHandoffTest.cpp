#include <exception>
#include <iostream>
#include <string>

#include "RuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_handoff_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-027-handoff";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "run";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.decision = "Allow";
  request.request_context["normalizer_ready"] = "true";
  request.request_context["request_id"] = "req-sidecar";
  request.request_context["session_id"] = "sess-sidecar";
  request.request_context["trace_id"] = "trace-sidecar";

  request.agent_request.request_id = "req-public";
  request.agent_request.session_id = "sess-public";
  request.agent_request.trace_id = "trace-public";
  request.agent_request.user_input = request.packet.payload;
  request.agent_request.request_channel = dasall::contracts::RequestChannel::Gateway;
  request.agent_request.created_at = 1700000000200;
  request.agent_request.idempotency_key = std::string("idem-public");
  return request;
}

void dispatch_forwards_public_agent_request_and_preserves_public_ids() {
  using dasall::access::AccessDisposition;
  using dasall::access::RuntimeBridge;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::access::RuntimeDispatchRequest captured_request;
  bool backend_called = false;

  RuntimeBridge bridge(
      [&captured_request, &backend_called](const auto& request) {
        backend_called = true;
        captured_request = request;
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {});

  const auto mapped = bridge.dispatch(make_handoff_request());

  assert_true(backend_called,
              "runtime bridge should forward a validated public AgentRequest to backend");
  assert_equal(std::string("req-public"),
               captured_request.agent_request.request_id.value_or(std::string()),
               "backend should observe request_id from public handoff instead of sidecar");
  assert_equal(std::string("sess-public"),
               mapped.response_context.at("session_id"),
               "response context should preserve session_id from public handoff");
  assert_equal(std::string("trace-public"),
               mapped.response_context.at("trace_id"),
               "response context should preserve trace_id from public handoff");
}

}  // namespace

int main() {
  try {
    dispatch_forwards_public_agent_request_and_preserves_public_ids();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}