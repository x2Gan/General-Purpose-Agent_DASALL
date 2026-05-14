#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include "AccessGatewayFactory.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::AccessObservabilityEmitBackend;
using dasall::access::GatewayAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct CapturedEvent {
  std::string name;
  std::map<std::string, std::string> fields;
};

void gateway_shutdown_timeout_emits_abandoned_audit_event() {
  std::vector<CapturedEvent> captured_events;

  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.observability_emit_backend =
      [&captured_events](std::string_view event_name,
                         const std::map<std::string, std::string>& fields) {
        captured_events.push_back(CapturedEvent{
            .name = std::string(event_name),
            .fields = fields,
        });
        return true;
      };
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "gateway shutdown audit test should build a concrete gateway");
  assert_true(gateway->init(),
              "gateway shutdown audit test should initialize the gateway");

  InboundPacket packet;
  packet.packet_id = "req-051-gateway-shutdown";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http_unary";
  packet.peer_ref = "jwt:user://tenant-a/alice";
  packet.payload = "run";

  std::thread inflight([&gateway, packet]() {
    (void)gateway->submit(packet);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  gateway->shutdown(std::chrono::milliseconds(1));

  if (inflight.joinable()) {
    inflight.join();
  }

  assert_equal(static_cast<std::size_t>(3), captured_events.size(),
               "gateway shutdown audit should preserve request, dispatch, and shutdown events");
  const auto shutdown_it = std::find_if(
      captured_events.begin(),
      captured_events.end(),
      [](const CapturedEvent& event) {
        return event.name == "daemon.shutdown.abandoned";
      });
  assert_true(shutdown_it != captured_events.end(),
              "gateway shutdown audit should emit shutdown abandoned event");
  assert_equal(std::string("gateway-access-gateway"),
               shutdown_it->fields.at("connection_ref"),
               "gateway shutdown audit should preserve gateway connection ref");
  assert_equal(std::string("1"),
               shutdown_it->fields.at("abandoned_requests"),
               "gateway shutdown audit should preserve abandoned request count");
}

}  // namespace

int main() {
  try {
    gateway_shutdown_timeout_emits_abandoned_audit_event();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessGatewayShutdownAuditTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}