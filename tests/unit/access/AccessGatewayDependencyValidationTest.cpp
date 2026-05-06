#include <exception>
#include <iostream>
#include <string>

#include "AccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void gateway_without_submit_pipeline_cannot_enter_ready_state() {
  using dasall::access::AccessDisposition;
  using dasall::access::AccessGateway;
  using dasall::access::AccessGatewayState;
  using dasall::access::InboundPacket;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessGateway gateway;

  assert_true(!gateway.init(),
              "gateway should fail-closed when submit pipeline is not configured");
  assert_equal(static_cast<int>(AccessGatewayState::Uninitialized),
               static_cast<int>(gateway.state()),
               "failed init should leave gateway outside Ready state");
  assert_true(!gateway.is_ready(),
              "failed init should keep readiness false");

  InboundPacket packet;
  packet.packet_id = "pkt-027-gateway-no-pipeline";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  const auto result = gateway.submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "gateway should reject submits while not ready");
  assert_equal(std::string("gateway_not_ready_or_shutting_down"),
               result.error_ref.value_or(std::string()),
               "failed init should surface not-ready rejection instead of pretending Ready");
}

}  // namespace

int main() {
  try {
    gateway_without_submit_pipeline_cannot_enter_ready_state();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}