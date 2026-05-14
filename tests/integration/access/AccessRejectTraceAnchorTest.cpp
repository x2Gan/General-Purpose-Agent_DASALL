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
    std::vector<CapturedEvent>* captured_events,
    bool emit_success) {
  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.publish_view.max_payload_bytes = 1024;
  options.observability_emit_backend =
      [captured_events, emit_success](const std::string_view event_name,
                                      const std::map<std::string, std::string>& fields) {
        captured_events->push_back(CapturedEvent{
            .name = std::string(event_name),
            .fields = fields,
        });
        return emit_success;
      };
  return options;
}

[[nodiscard]] InboundPacket make_authenticated_packet(std::string request_id) {
  InboundPacket packet;
  packet.packet_id = std::move(request_id);
  packet.entry_type = "gateway";
  packet.protocol_kind = "http_unary";
  packet.peer_ref = "jwt:user://tenant-a/alice";
  packet.payload = "run";
  packet.trace_id = std::string("trace-048-reject-anchor");
  packet.session_hint = std::string("sess-048-reject-anchor");
  return packet;
}

void auth_failure_preserves_trace_anchors_when_observability_sink_fails() {
  std::vector<CapturedEvent> captured_events;
  int runtime_call_count = 0;

  auto options = make_base_options(&captured_events, false);
  options.auth_view.auth_provider_ref = std::string("secret://unavailable");
  options.runtime_dispatch_backend = [&runtime_call_count](const RuntimeDispatchRequest&) {
    ++runtime_call_count;
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "reject trace anchor integration should build a concrete gateway");
  assert_true(gateway->init(),
              "reject trace anchor integration should initialize the gateway");

  const auto result = gateway->submit(make_authenticated_packet("req-048-auth-failed"));
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "auth failure should remain rejected even when observability sink fails");
  assert_true(result.error_ref.has_value(),
              "auth failure should preserve reject reason");
  assert_equal(std::string("secret_backend_unavailable"),
               *result.error_ref,
               "auth failure should preserve backend unavailable reason");
  assert_equal(std::string("req-048-auth-failed"),
               result.response_context.at("request_id"),
               "auth failure should preserve request_id on rejected result");
  assert_equal(std::string("sess-048-reject-anchor"),
               result.response_context.at("session_id"),
               "auth failure should preserve session_id on rejected result");
  assert_equal(std::string("trace-048-reject-anchor"),
               result.response_context.at("trace_id"),
               "auth failure should preserve trace_id on rejected result");
  assert_equal(0,
               runtime_call_count,
               "auth failure should not reach runtime dispatch");
  assert_equal(2,
               static_cast<int>(captured_events.size()),
               "auth failure should still invoke request and auth observability sinks");
  assert_equal(std::string("access.auth.failed"),
               captured_events[1].name,
               "auth failure should emit auth_failed event second");
  assert_equal(std::string("req-048-auth-failed"),
               captured_events[1].fields.at("request_id"),
               "auth failure event should preserve request_id");
  assert_equal(std::string("trace-048-reject-anchor"),
               captured_events[1].fields.at("trace_id"),
               "auth failure event should preserve trace_id");
}

}  // namespace

int main() {
  try {
    auth_failure_preserves_trace_anchors_when_observability_sink_fails();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessRejectTraceAnchorTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}