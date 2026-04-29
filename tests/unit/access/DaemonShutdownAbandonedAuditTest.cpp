#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "AccessGateway.h"
#include "AccessObservabilityBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::AccessGateway;
using dasall::access::AccessObservabilityBridge;
using dasall::access::AccessObservabilityEvent;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void shutdown_timeout_emits_daemon_abandoned_audit_event() {
  std::vector<AccessObservabilityEvent> captured;
  auto bridge = std::make_shared<AccessObservabilityBridge>(
      [&captured](const AccessObservabilityEvent& event) {
        captured.push_back(event);
        return true;
      });

  AccessGateway gateway(
      [](const InboundPacket&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      },
      {},
      [bridge](std::size_t abandoned_requests) {
        (void)bridge->emit_shutdown_abandoned(
            "DRAINING",
            "daemon-access-gateway",
            static_cast<std::uint32_t>(abandoned_requests));
      });

  assert_true(gateway.init(),
              "gateway should initialize before abandoned audit shutdown test");

  InboundPacket packet;
  packet.packet_id = "pkt-022-abandoned-audit";
  packet.entry_type = "daemon.control";
  packet.protocol_kind = "unix";

  std::thread inflight([&gateway, packet]() {
    (void)gateway.submit(packet);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  gateway.shutdown(std::chrono::milliseconds(1));

  if (inflight.joinable()) {
    inflight.join();
  }

  assert_equal(static_cast<std::size_t>(1), captured.size(),
               "abandoned shutdown path should emit one audit event");
  assert_equal(std::string("daemon.shutdown.abandoned"), captured.front().name,
               "abandoned shutdown path should use daemon shutdown audit event name");
  assert_equal(std::string("DRAINING"), captured.front().fields.at("daemon_state"),
               "abandoned shutdown audit should preserve daemon_state");
  assert_equal(std::string("daemon-access-gateway"),
               captured.front().fields.at("connection_ref"),
               "abandoned shutdown audit should preserve connection_ref");
  assert_equal(std::string("1"), captured.front().fields.at("abandoned_requests"),
               "abandoned shutdown audit should encode abandoned inflight count");
}

}  // namespace

int main() {
  try {
    shutdown_timeout_emits_daemon_abandoned_audit_event();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonShutdownAbandonedAuditTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
