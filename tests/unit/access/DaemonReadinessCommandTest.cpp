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
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

std::shared_ptr<dasall::access::IAccessGateway> build_gateway(bool bridge_reachable) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_version = "v1";
  options.daemon_profile_id = "daemon.test";
  options.daemon_bridge_reachable = bridge_reachable;
  options.runtime_dispatch_backend = bridge_reachable
      ? DaemonAccessPipelineOptions::RuntimeDispatchBackend{[](const auto&) {
          return dasall::access::RuntimeDispatchResult{};
        }}
      : DaemonAccessPipelineOptions::RuntimeDispatchBackend{};

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "gateway should be created for readiness command test");
  assert_true(gateway->init(), "gateway should initialize for readiness command test");
  return gateway;
}

void readiness_returns_not_ready_when_bridge_unavailable() {
  auto gateway = build_gateway(false);

  InboundPacket packet;
  packet.packet_id = "readiness";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "readiness should reject when runtime bridge is unavailable");
  assert_true(result.publish_envelope.has_value(),
              "readiness should still produce an envelope for not ready state");
  assert_true(result.publish_envelope->payload.find("\"state\":\"NOT_READY\"") != std::string::npos,
              "readiness payload should encode NOT_READY state");
}

}  // namespace

int main() {
  try {
    readiness_returns_not_ready_when_bridge_unavailable();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonReadinessCommandTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
