#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "AccessObservabilityBridge.h"
#include "support/TestAssertions.h"

namespace {

void emit_publish_failed_contains_required_field_set() {
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessObservabilityBridge;
  using dasall::access::AccessObservabilityEvent;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessObservabilityEvent captured;
  bool invoked = false;
  AccessObservabilityBridge bridge(
      [&captured, &invoked](const AccessObservabilityEvent& event) {
        invoked = true;
        captured = event;
        return false;
      });

  dasall::access::PublishEnvelope envelope;
  envelope.request_id = "req-023-fields";
  envelope.session_id = "sess-023-fields";
  envelope.trace_id = "trace-023-fields";
  envelope.result_id = "result-023-fields";
  envelope.protocol_kind = "http";

  const bool emitted = bridge.emit_publish_failed(
      envelope,
      AccessErrorCode::PublishChannelUnavailable,
      "socket_write_failed");

  assert_true(!emitted,
              "bridge should return backend result so caller can observe emission failure");
  assert_true(invoked, "bridge should invoke backend even when backend returns false");

  assert_equal(std::string("access.publish.failed"), captured.name,
               "event name should be publish_failed");
  assert_true(captured.fields.contains("request_id"), "field set should contain request_id");
  assert_true(captured.fields.contains("session_id"), "field set should contain session_id");
  assert_true(captured.fields.contains("trace_id"), "field set should contain trace_id");
  assert_true(captured.fields.contains("result_id"), "field set should contain result_id");
  assert_true(captured.fields.contains("error_code"), "field set should contain error_code");
  assert_equal(std::string("req-023-fields"), captured.fields.at("request_id"),
               "request_id should be preserved");
  assert_equal(std::to_string(static_cast<int>(AccessErrorCode::PublishChannelUnavailable)),
               captured.fields.at("error_code"),
               "error_code should be encoded using AccessErrorCode numeric value");
}

void emit_daemon_events_contain_required_field_set() {
  using dasall::access::AccessObservabilityBridge;
  using dasall::access::AccessObservabilityEvent;
  using dasall::access::InboundPacket;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::vector<AccessObservabilityEvent> captured;
  AccessObservabilityBridge bridge(
      [&captured](const AccessObservabilityEvent& event) {
        captured.push_back(event);
        return true;
      });

  InboundPacket packet;
  packet.packet_id = "req-021-fields";
  packet.entry_type = "daemon.control";
  packet.protocol_kind = "unix";
  packet.peer_ref = "peer://daemon/local";

  const bool request_emitted = bridge.emit_daemon_request_fact(
      packet,
      "req-021-fields",
      "sess-021-fields",
      "trace-021-fields",
      "ready",
      "conn-021");
  const bool receipt_emitted = bridge.emit_receipt_event(
      "req-021-fields",
      "sess-021-fields",
      "trace-021-fields",
      "ready",
      "receipt-021");
  const bool denied_emitted = bridge.emit_peer_identity_denied(
      "req-021-fields",
      "trace-021-fields",
      "not_ready",
      "conn-021");
  const bool shutdown_emitted = bridge.emit_shutdown_abandoned(
      "draining",
      "conn-021",
      3);

  assert_true(request_emitted && receipt_emitted && denied_emitted && shutdown_emitted,
              "daemon observability events should return backend success");
  assert_equal(static_cast<std::size_t>(4), captured.size(),
               "daemon observability flow should emit four events");

  const auto& request_event = captured.at(0);
  assert_equal(std::string("daemon.request.fact"), request_event.name,
               "request event should use daemon request fact name");
  assert_equal(std::string("req-021-fields"), request_event.fields.at("request_id"),
               "request event should preserve request_id");
  assert_equal(std::string("sess-021-fields"), request_event.fields.at("session_id"),
               "request event should preserve session_id");
  assert_equal(std::string("trace-021-fields"), request_event.fields.at("trace_id"),
               "request event should preserve trace_id");
  assert_equal(std::string("ready"), request_event.fields.at("daemon_state"),
               "request event should preserve daemon_state");
  assert_equal(std::string("conn-021"), request_event.fields.at("connection_ref"),
               "request event should preserve connection_ref");
  assert_equal(std::string("daemon.control"), request_event.fields.at("entry_type"),
               "request event should preserve entry_type");
  assert_equal(std::string("unix"), request_event.fields.at("protocol_kind"),
               "request event should preserve protocol kind");

  const auto& receipt_event = captured.at(1);
  assert_equal(std::string("daemon.receipt.event"), receipt_event.name,
               "receipt event should use daemon receipt name");
  assert_equal(std::string("receipt-021"), receipt_event.fields.at("receipt_ref"),
               "receipt event should preserve receipt ref");

  const auto& denied_event = captured.at(2);
  assert_equal(std::string("daemon.peer_identity.denied"), denied_event.name,
               "denied event should use peer identity denied name");
  assert_equal(std::string("not_ready"), denied_event.fields.at("daemon_state"),
               "denied event should preserve daemon state");
  assert_equal(std::string("conn-021"), denied_event.fields.at("connection_ref"),
               "denied event should preserve connection ref");

  const auto& shutdown_event = captured.at(3);
  assert_equal(std::string("daemon.shutdown.abandoned"), shutdown_event.name,
               "shutdown event should use shutdown abandoned name");
  assert_equal(std::string("draining"), shutdown_event.fields.at("daemon_state"),
               "shutdown event should preserve daemon state");
  assert_equal(std::string("3"), shutdown_event.fields.at("abandoned_requests"),
               "shutdown event should encode abandoned requests as string");
}

}  // namespace

int main() {
  try {
    emit_publish_failed_contains_required_field_set();
    emit_daemon_events_contain_required_field_set();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
