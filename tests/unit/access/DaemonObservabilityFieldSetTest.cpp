#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "AccessObservabilityBridge.h"
#include "support/TestAssertions.h"

namespace {

void daemon_observability_events_expose_required_field_set() {
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

  assert_true(bridge.emit_daemon_request_fact(
                  packet,
                  "req-021-fields",
                  "sess-021-fields",
                  "trace-021-fields",
                  "ready",
                  "conn-021"),
              "request fact emission should return backend success");
  assert_true(bridge.emit_receipt_event(
                  "req-021-fields",
                  "sess-021-fields",
                  "trace-021-fields",
                  "ready",
                  "receipt-021"),
              "receipt event emission should return backend success");
  assert_true(bridge.emit_peer_identity_denied(
                  "req-021-fields",
                  "trace-021-fields",
                  "not_ready",
                  "conn-021"),
              "peer identity denied emission should return backend success");
  assert_true(bridge.emit_shutdown_abandoned(
                  "draining",
                  "conn-021",
                  3),
              "shutdown abandoned emission should return backend success");
  assert_true(bridge.emit_reload_denied(
                  "ready",
                  "daemon.socket_path",
                  "reload_rejected_restart_only_keys"),
              "reload denied emission should return backend success");

  assert_equal(static_cast<std::size_t>(5), captured.size(),
               "daemon observability test should capture five events");

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
               "request event should preserve protocol_kind");
  assert_true(!request_event.fields.contains("auth_secret"),
              "request event must not log authentication secrets");

  const auto& receipt_event = captured.at(1);
  assert_equal(std::string("daemon.receipt.event"), receipt_event.name,
               "receipt event should use daemon receipt name");
  assert_equal(std::string("receipt-021"), receipt_event.fields.at("receipt_ref"),
               "receipt event should preserve receipt_ref");

  const auto& denied_event = captured.at(2);
  assert_equal(std::string("daemon.peer_identity.denied"), denied_event.name,
               "denied event should use peer identity denied name");
  assert_equal(std::string("not_ready"), denied_event.fields.at("daemon_state"),
               "denied event should preserve daemon_state");
  assert_equal(std::string("conn-021"), denied_event.fields.at("connection_ref"),
               "denied event should preserve connection_ref");

  const auto& shutdown_event = captured.at(3);
  assert_equal(std::string("daemon.shutdown.abandoned"), shutdown_event.name,
               "shutdown event should use shutdown abandoned name");
  assert_equal(std::string("draining"), shutdown_event.fields.at("daemon_state"),
               "shutdown event should preserve daemon_state");
  assert_equal(std::string("3"), shutdown_event.fields.at("abandoned_requests"),
               "shutdown event should encode abandoned request count");

  const auto& reload_denied_event = captured.at(4);
  assert_equal(std::string("daemon.reload.denied"), reload_denied_event.name,
               "reload denied event should use daemon reload denied name");
  assert_equal(std::string("ready"), reload_denied_event.fields.at("daemon_state"),
               "reload denied event should preserve daemon_state");
  assert_equal(std::string("daemon.socket_path"), reload_denied_event.fields.at("rejected_key"),
               "reload denied event should preserve rejected key");
  assert_equal(std::string("reload_rejected_restart_only_keys"), reload_denied_event.fields.at("reason_code"),
               "reload denied event should preserve reason code");
}

}  // namespace

int main() {
  try {
    daemon_observability_events_expose_required_field_set();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
