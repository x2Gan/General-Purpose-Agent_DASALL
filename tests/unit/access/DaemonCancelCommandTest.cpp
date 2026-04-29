#include <exception>
#include <iostream>
#include <optional>
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

struct CancelBackendProbe {
  int calls = 0;
  std::string last_request_id;
  std::string last_actor_ref;
  bool should_succeed = true;
};

[[nodiscard]] std::shared_ptr<dasall::access::IAccessGateway> build_gateway(
    CancelBackendProbe* probe) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 1024;

  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::AcceptedAsync;
    result.receipt_ref = "runtime-task-ref";
    return result;
  };

  options.runtime_cancel_backend = [probe](std::string_view request_id,
                                           std::string_view actor_ref) {
    ++probe->calls;
    probe->last_request_id = std::string(request_id);
    probe->last_actor_ref = std::string(actor_ref);
    return probe->should_succeed;
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "daemon access gateway factory should return a gateway");
  assert_true(gateway->init(), "daemon access gateway should initialize");
  return gateway;
}

[[nodiscard]] RuntimeDispatchResult submit_async_request(
    const std::shared_ptr<dasall::access::IAccessGateway>& gateway,
    const std::string& request_id) {
  InboundPacket packet;
  packet.packet_id = request_id;
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "run";
  packet.async_preferred = true;
  return gateway->submit(packet);
}

void cancel_owner_match_forwards_runtime_cancel() {
  CancelBackendProbe probe;
  auto gateway = build_gateway(&probe);

  const auto async_result = submit_async_request(gateway, "req-cancel-ok");
  assert_equal(static_cast<int>(AccessDisposition::AcceptedAsync),
               static_cast<int>(async_result.disposition),
               "async submit should be accepted");
  assert_true(async_result.publish_envelope.has_value(),
              "accepted async should carry publish envelope");
  assert_true(async_result.publish_envelope->receipt.has_value(),
              "accepted async should carry receipt for follow-up cancel");

  const auto& receipt = async_result.publish_envelope->receipt.value();

  InboundPacket cancel_packet;
  cancel_packet.packet_id = "cancel";
  cancel_packet.entry_type = "daemon";
  cancel_packet.protocol_kind = "ipc_uds";
  cancel_packet.peer_ref = "local_trusted:1000";
  cancel_packet.payload = "receipt_ref=" + receipt.receipt_id +
                          ";ownership_token=" + receipt.ownership_token +
                          ";actor_ref=local://uid/1000";

  const auto cancel_result = gateway->submit(cancel_packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(cancel_result.disposition),
               "cancel should complete when owner matches");
  assert_true(cancel_result.publish_envelope.has_value(),
              "cancel path should emit publish envelope");
  assert_equal(std::string("cancelled"), cancel_result.publish_envelope->payload,
               "cancel result payload should be cancelled");

  assert_equal(1, probe.calls,
               "owner-matched cancel should forward runtime cancel exactly once");
  assert_equal(receipt.request_id, probe.last_request_id,
               "runtime cancel should use receipt-bound request_id");
  assert_equal(std::string("local://uid/1000"), probe.last_actor_ref,
               "runtime cancel should use authenticated actor_ref");
}

void cancel_owner_mismatch_fails_closed_and_does_not_forward() {
  CancelBackendProbe probe;
  auto gateway = build_gateway(&probe);

  const auto async_result = submit_async_request(gateway, "req-cancel-mismatch");
  assert_true(async_result.publish_envelope.has_value() &&
                  async_result.publish_envelope->receipt.has_value(),
              "accepted async should create receipt before mismatch check");
  const auto& receipt = async_result.publish_envelope->receipt.value();

  InboundPacket cancel_packet;
  cancel_packet.packet_id = "cancel";
  cancel_packet.entry_type = "daemon";
  cancel_packet.protocol_kind = "ipc_uds";
  cancel_packet.peer_ref = "local_trusted:1000";
  cancel_packet.payload = "receipt_ref=" + receipt.receipt_id +
                          ";ownership_token=invalid-token" +
                          ";actor_ref=local://uid/1000";

  const auto cancel_result = gateway->submit(cancel_packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(cancel_result.disposition),
               "owner mismatch should be rejected");
  assert_true(cancel_result.error_ref.has_value(),
              "owner mismatch should expose stable reject reason");
  assert_equal(std::string("cancel_owner_mismatch"), *cancel_result.error_ref,
               "owner mismatch should map to cancel_owner_mismatch error_ref");
  assert_equal(0, probe.calls,
               "owner mismatch must not forward runtime cancel");
}

}  // namespace

int main() {
  try {
    cancel_owner_match_forwards_runtime_cancel();
    cancel_owner_mismatch_fails_closed_and_does_not_forward();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonCancelCommandTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
