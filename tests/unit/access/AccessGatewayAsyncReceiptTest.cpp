#include <exception>
#include <iostream>
#include <string>

#include "AccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void submit_propagates_accepted_async_receipt() {
  using dasall::access::AccessDisposition;
  using dasall::access::AccessGateway;
  using dasall::access::InboundPacket;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessGateway gateway(
      [](const InboundPacket& packet) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::AcceptedAsync;
        result.receipt_ref = "receipt:" + packet.packet_id;
        return result;
      },
      {});

  assert_true(gateway.init(), "gateway init should succeed");

  InboundPacket packet;
  packet.packet_id = "pkt-024-async";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http";

  const auto result = gateway.submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::AcceptedAsync),
               static_cast<int>(result.disposition),
               "submit should propagate accepted async disposition");
  assert_true(result.receipt_ref.has_value(),
              "accepted async result should carry receipt ref");
  assert_equal(std::string("receipt:pkt-024-async"), *result.receipt_ref,
               "receipt should be preserved from submit pipeline output");
}

}  // namespace

int main() {
  try {
    submit_propagates_accepted_async_receipt();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
