#include <exception>
#include <iostream>
#include <string>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void gateway_factory_hands_public_agent_request_to_runtime_backend() {
  using dasall::access::AccessDisposition;
  using dasall::access::GatewayAccessPipelineOptions;
  using dasall::access::InboundPacket;
  using dasall::access::RuntimeDispatchRequest;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeDispatchRequest captured_request;
  int runtime_call_count = 0;

  GatewayAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.publish_view.max_payload_bytes = 64;
  options.runtime_dispatch_backend =
      [&captured_request, &runtime_call_count](const RuntimeDispatchRequest& request) {
        captured_request = request;
        ++runtime_call_count;
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      };

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "gateway factory should return a concrete gateway");
  assert_true(gateway->init(),
              "gateway access factory should initialize when runtime backend is configured");

  InboundPacket packet;
  packet.packet_id = "req-027-gateway-compose";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http_unary";
  packet.peer_ref = "jwt:user://tenant-a/alice";
  packet.payload = "run";
  packet.trace_id = std::string("trace-027-gateway-compose");
  packet.session_hint = std::string("sess-027-gateway-compose");

  const auto result = gateway->submit(packet);

  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "gateway factory should route valid submit to runtime backend");
  assert_equal(1,
               runtime_call_count,
               "gateway factory should call runtime backend exactly once for valid submit");
  assert_equal(std::string("req-027-gateway-compose"),
               captured_request.agent_request.request_id.value_or(std::string()),
               "gateway factory should forward request_id via public AgentRequest handoff");
  assert_equal(std::string("sess-027-gateway-compose"),
               captured_request.agent_request.session_id.value_or(std::string()),
               "gateway factory should forward session_hint via public AgentRequest handoff");
  assert_equal(std::string("trace-027-gateway-compose"),
               captured_request.agent_request.trace_id.value_or(std::string()),
               "gateway factory should forward trace_id via public AgentRequest handoff");
  assert_equal(std::string("run"),
               captured_request.agent_request.user_input.value_or(std::string()),
               "gateway factory should forward user input through public AgentRequest handoff");
}

}  // namespace

int main() {
  try {
    gateway_factory_hands_public_agent_request_to_runtime_backend();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}