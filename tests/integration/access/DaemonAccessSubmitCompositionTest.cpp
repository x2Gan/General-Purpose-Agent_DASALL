#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void daemon_factory_hands_public_agent_request_to_runtime_backend() {
  using dasall::access::AccessDisposition;
  using dasall::access::DaemonAccessPipelineOptions;
  using dasall::access::InboundPacket;
  using dasall::access::RuntimeDispatchRequest;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RuntimeDispatchRequest captured_request;
  int runtime_call_count = 0;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 64;
  options.runtime_dispatch_backend =
      [&captured_request, &runtime_call_count](const RuntimeDispatchRequest& request) {
        captured_request = request;
        ++runtime_call_count;
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "daemon access factory should return a concrete gateway");
  assert_true(gateway->init(),
              "daemon access gateway should initialize when runtime backend is configured");

  InboundPacket packet;
  packet.packet_id = "req-027-daemon-compose";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "ok";
  packet.trace_id = std::string("trace-027-daemon-compose");
  packet.session_hint = std::string("sess-027-daemon-compose");

  const auto result = gateway->submit(packet);

  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "daemon factory should route valid submit to runtime backend");
  assert_equal(1,
               runtime_call_count,
               "daemon factory should call runtime backend exactly once for valid submit");
  assert_equal(std::string("req-027-daemon-compose"),
               captured_request.agent_request.request_id.value_or(std::string()),
               "daemon factory should forward request_id via public AgentRequest handoff");
  assert_equal(std::string("sess-027-daemon-compose"),
               captured_request.agent_request.session_id.value_or(std::string()),
               "daemon factory should forward session_hint via public AgentRequest handoff");
  assert_equal(std::string("trace-027-daemon-compose"),
               captured_request.agent_request.trace_id.value_or(std::string()),
               "daemon factory should forward trace_id via public AgentRequest handoff");
  assert_equal(std::string("ok"),
               captured_request.agent_request.user_input.value_or(std::string()),
               "daemon factory should forward user input through public AgentRequest handoff");
}

}  // namespace

int main() {
  try {
    daemon_factory_hands_public_agent_request_to_runtime_backend();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}