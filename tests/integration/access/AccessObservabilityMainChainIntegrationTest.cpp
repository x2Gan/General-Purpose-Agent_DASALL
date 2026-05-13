#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
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

[[nodiscard]] GatewayAccessPipelineOptions make_base_options(
    std::vector<CapturedEvent>* captured_events) {
  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.publish_view.max_payload_bytes = 1024;
  options.observability_emit_backend =
      [captured_events](const std::string_view event_name,
                        const std::map<std::string, std::string>& fields) {
        captured_events->push_back(CapturedEvent{
            .name = std::string(event_name),
            .fields = fields,
        });
        return true;
      };
  return options;
}

[[nodiscard]] InboundPacket make_packet(std::string request_id) {
  InboundPacket packet;
  packet.packet_id = std::move(request_id);
  packet.entry_type = "gateway";
  packet.protocol_kind = "http_unary";
  packet.peer_ref = "jwt:user://tenant-a/alice";
  packet.payload = "run";
  packet.trace_id = std::string("trace-028-observability");
  packet.session_hint = std::string("sess-028-observability");
  return packet;
}

void successful_submit_emits_request_received_and_dispatch_events() {
  std::vector<CapturedEvent> captured_events;
  auto options = make_base_options(&captured_events);
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "observability integration should build a concrete gateway");
  assert_true(gateway->init(),
              "observability integration should initialize the gateway");

  const auto result = gateway->submit(make_packet("req-028-observability-ok"));
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "observability integration should keep successful submit on the main path");
  assert_equal(2,
               static_cast<int>(captured_events.size()),
               "observability integration should emit request and dispatch events on success");
  assert_equal(std::string("access.request.received"),
               captured_events[0].name,
               "observability integration should emit request_received first");
  assert_equal(std::string("req-028-observability-ok"),
               captured_events[0].fields.at("request_id"),
               "request_received should preserve request_id");
  assert_equal(std::string("access.runtime.dispatched"),
               captured_events[1].name,
               "observability integration should emit runtime dispatch second");
  assert_equal(std::string("gateway"),
               captured_events[1].fields.at("entry_type"),
               "dispatch event should preserve entry_type");
}

void policy_backend_unavailable_emits_denied_event_and_skips_runtime() {
  std::vector<CapturedEvent> captured_events;
  int runtime_call_count = 0;

  auto options = make_base_options(&captured_events);
  options.policy_backend_available = false;
  options.runtime_dispatch_backend = [&runtime_call_count](const RuntimeDispatchRequest&) {
    ++runtime_call_count;
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "policy unavailable integration should build a concrete gateway");
  assert_true(gateway->init(),
              "policy unavailable integration should initialize the gateway");

  const auto result = gateway->submit(make_packet("req-028-policy-unavailable"));
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "policy unavailable integration should fail closed before runtime dispatch");
  assert_true(result.error_ref.has_value(),
              "policy unavailable integration should expose stable reject reason");
  assert_equal(std::string("policy_backend_unavailable"),
               *result.error_ref,
               "policy unavailable integration should preserve backend unavailable reason");
  assert_equal(0,
               runtime_call_count,
               "policy unavailable integration should not reach runtime dispatch");
  assert_equal(2,
               static_cast<int>(captured_events.size()),
               "policy unavailable integration should emit request and denied events");
  assert_equal(std::string("access.policy.denied"),
               captured_events[1].name,
               "policy unavailable integration should emit policy_denied second");
  assert_equal(std::string("policy_backend_unavailable"),
               captured_events[1].fields.at("reason_code"),
               "policy denied event should preserve backend unavailable reason");
}

}  // namespace

int main() {
  try {
    successful_submit_emits_request_received_and_dispatch_events();
    policy_backend_unavailable_emits_denied_event_and_skips_runtime();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessObservabilityMainChainIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}