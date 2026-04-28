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

std::shared_ptr<dasall::access::IAccessGateway> build_gateway(int* runtime_call_count) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 8;
  options.runtime_dispatch_backend =
      [runtime_call_count](const RuntimeDispatchRequest&) {
        ++(*runtime_call_count);
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "daemon access gateway factory should return a gateway");
  assert_true(gateway->init(), "daemon access gateway should initialize");
  return gateway;
}

void unknown_command_is_rejected_before_runtime() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "unknown_command";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "ok";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "unknown command should be rejected");
  assert_equal(0,
               runtime_call_count,
               "unknown command should not reach runtime backend");
}

void auth_deny_is_rejected_before_runtime() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "req-auth-deny";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "untrusted";
  packet.payload = "ok";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "auth deny path should be rejected");
  assert_equal(0,
               runtime_call_count,
               "auth deny path should not reach runtime backend");
}

void payload_too_large_is_rejected_before_runtime() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "req-large-payload";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "payload-too-large";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "payload too large should be rejected");
  assert_equal(0,
               runtime_call_count,
               "payload too large should not reach runtime backend");
}

void valid_submit_reaches_runtime_pipeline() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "req-ok";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "ok";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "valid submit should reach runtime backend");
  assert_equal(1,
               runtime_call_count,
               "valid submit should call runtime backend exactly once");
}

}  // namespace

int main() {
  try {
    unknown_command_is_rejected_before_runtime();
    auth_deny_is_rejected_before_runtime();
    payload_too_large_is_rejected_before_runtime();
    valid_submit_reaches_runtime_pipeline();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonAccessPipelineFactoryTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
