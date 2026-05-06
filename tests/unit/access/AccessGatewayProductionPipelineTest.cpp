#include <exception>
#include <iostream>
#include <string>

#include "AccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void configured_submit_pipeline_can_initialize_and_dispatch() {
  using dasall::access::AccessDisposition;
  using dasall::access::AccessGateway;
  using dasall::access::InboundPacket;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int submit_calls = 0;
  AccessGateway gateway(
      [&submit_calls](const InboundPacket& packet) {
        ++submit_calls;
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        result.response_context["request_id"] = packet.packet_id;
        return result;
      },
      {});

  assert_true(gateway.init(),
              "configured access gateway should initialize into Ready state");

  InboundPacket packet;
  packet.packet_id = "pkt-027-gateway-production";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  const auto result = gateway.submit(packet);

  assert_equal(1,
               submit_calls,
               "configured submit pipeline should be invoked exactly once");
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "configured access gateway should dispatch after init");
}

}  // namespace

int main() {
  try {
    configured_submit_pipeline_can_initialize_and_dispatch();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}