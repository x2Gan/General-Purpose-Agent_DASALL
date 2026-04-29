#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void ping_is_handled_by_command_router_without_runtime_backend() {
  int runtime_call_count = 0;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_version = "v1";
  options.daemon_profile_id = "daemon.router.test";
  options.runtime_dispatch_backend =
      [&runtime_call_count](const RuntimeDispatchRequest&) {
        ++runtime_call_count;
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "daemon access gateway should be created for router regression test");
  assert_true(gateway->init(), "daemon access gateway should initialize for router regression test");

  InboundPacket packet;
  packet.packet_id = "ping";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";

  const auto result = gateway->submit(packet);

  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "ping should be completed by daemon command router");
  assert_equal(0, runtime_call_count,
               "ping should not bypass daemon command router into runtime backend");
  assert_true(result.publish_envelope.has_value(),
              "ping should return a publish envelope from router path");
  assert_true(result.publish_envelope->payload.find("\"readiness\":\"READY\"") != std::string::npos,
              "router-handled ping should encode readiness summary in payload");
}

}  // namespace

int main() {
  try {
    ping_is_handled_by_command_router_without_runtime_backend();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonPingDoesNotBypassRouterTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
