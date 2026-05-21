#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

std::shared_ptr<dasall::access::IAccessGateway> build_gateway(
    bool bridge_reachable,
    std::string runtime_readiness_label = "default-ready",
    std::vector<std::string> degraded_reasons = {}) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_version = "v1";
  options.daemon_profile_id = "daemon.test";
  options.daemon_bridge_reachable = bridge_reachable;
  options.daemon_runtime_readiness_label = std::move(runtime_readiness_label);
  options.daemon_runtime_degraded_reasons = std::move(degraded_reasons);
  options.runtime_dispatch_backend = DaemonAccessPipelineOptions::RuntimeDispatchBackend{
      [](const auto&) { return dasall::access::RuntimeDispatchResult{}; }};

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

void readiness_surfaces_degraded_runtime_readiness_without_failing_bridge() {
  auto gateway = build_gateway(
      true,
      "degraded-ready",
      {"runtime_optional_port_gap", "runtime_missing_optional:knowledge",
       "runtime_missing_optional:llm"});

  InboundPacket packet;
  packet.packet_id = "readiness";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "degraded runtime readiness should keep readiness query successful when bridge is reachable");
  assert_true(result.publish_envelope.has_value(),
              "degraded readiness should still produce an envelope");
  assert_true(result.publish_envelope->payload.find("\"state\":\"DEGRADED\"") != std::string::npos,
              "readiness payload should encode DEGRADED state");
  assert_true(result.publish_envelope->payload.find("\"runtime_readiness\":\"degraded-ready\"") != std::string::npos,
              "readiness payload should expose degraded runtime readiness label");
  assert_true(result.publish_envelope->payload.find("runtime_entrypoint_degraded_ready") != std::string::npos,
              "readiness payload should include runtime degraded reason");
  assert_true(result.publish_envelope->payload.find("runtime_optional_port_gap") != std::string::npos,
              "readiness payload should include runtime optional-port degraded reason");
  assert_true(result.publish_envelope->payload.find("runtime_missing_optional:knowledge") != std::string::npos,
              "readiness payload should include missing knowledge degraded reason");
}

}  // namespace

int main() {
  try {
    readiness_returns_not_ready_when_bridge_unavailable();
    readiness_surfaces_degraded_runtime_readiness_without_failing_bridge();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonReadinessCommandTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
