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

std::shared_ptr<dasall::access::IAccessGateway> build_gateway() {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_version = "v1";
  options.daemon_profile_id = "daemon.test";
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "gateway should be created for ping command test");
  assert_true(gateway->init(), "gateway should initialize for ping command test");
  return gateway;
}

void ping_returns_version_and_readiness_summary() {
  auto gateway = build_gateway();

  InboundPacket packet;
  packet.packet_id = "ping";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "ping should complete through daemon command router");
  assert_true(result.publish_envelope.has_value(),
              "ping should produce a publish envelope");
  assert_true(result.publish_envelope->payload.find("\"daemon_version\":\"v1\"") != std::string::npos,
              "ping payload should contain daemon version");
  assert_true(result.publish_envelope->payload.find("\"readiness\":\"READY\"") != std::string::npos,
              "ping payload should contain readiness summary");
}

}  // namespace

int main() {
  try {
    ping_returns_version_and_readiness_summary();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonPingCommandTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
