#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "AccessObservabilityBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::InboundPacket make_packet() {
  dasall::access::InboundPacket packet;
  packet.packet_id = "pkt-023-obsv";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";
  packet.peer_ref = "peer://127.0.0.1";
  return packet;
}

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet = make_packet();
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.decision_proof.policy_decision_ref = "policy:allow:1";
  request.request_context["request_id"] = "req-023-obsv";
  request.request_context["session_id"] = "sess-023-obsv";
  request.request_context["trace_id"] = "trace-023-obsv";
  request.request_context["operation"] = "access.request.submit";
  request.request_context["target_type"] = "access.entry";
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_dispatch_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::Completed;
  return result;
}

void emits_all_access_observability_events() {
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessObservabilityBridge;
  using dasall::access::AccessObservabilityEvent;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::vector<AccessObservabilityEvent> captured;
  AccessObservabilityBridge bridge(
      [&captured](const AccessObservabilityEvent& event) {
        captured.push_back(event);
        return true;
      });

  const auto packet = make_packet();
  const auto request = make_request();
  const auto dispatch_result = make_dispatch_result();

  dasall::access::PublishEnvelope envelope;
  envelope.request_id = "req-023-obsv";
  envelope.session_id = "sess-023-obsv";
  envelope.trace_id = "trace-023-obsv";
  envelope.result_id = "result-023-obsv";
  envelope.protocol_kind = "http";

  assert_true(bridge.emit_request_received(packet, "req-023-obsv", "sess-023-obsv", "trace-023-obsv", "user://tenant-a/alice"),
              "request_received event should be emitted");
  assert_true(bridge.emit_auth_failed(packet, "req-023-obsv", "trace-023-obsv", "token_invalid", "user://tenant-a/alice"),
              "auth_failed event should be emitted");
  assert_true(bridge.emit_policy_denied(request, "policy_denied"),
              "policy_denied event should be emitted");
  assert_true(bridge.emit_dispatch_result(request, dispatch_result, 12),
              "dispatch_result event should be emitted");
  assert_true(bridge.emit_publish_failed(envelope, AccessErrorCode::PublishChannelUnavailable, "channel_closed"),
              "publish_failed event should be emitted");

  assert_equal(5, static_cast<int>(captured.size()),
               "bridge should emit exactly five events in this scenario");
  assert_equal(std::string("access.request.received"), captured[0].name,
               "first event should be request_received");
  assert_equal(std::string("access.publish.failed"), captured[4].name,
               "last event should be publish_failed");
}

}  // namespace

int main() {
  try {
    emits_all_access_observability_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
