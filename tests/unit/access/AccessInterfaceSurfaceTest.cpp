#include <exception>
#include <iostream>
#include <type_traits>

#include "AccessErrors.h"
#include "AccessTypes.h"
#include "IAccessGateway.h"
#include "IAccessRuntimeBridge.h"
#include "IAdmissionController.h"
#include "IProtocolAdapter.h"
#include "support/TestAssertions.h"

namespace {

void access_public_surface_is_discoverable() {
  static_assert(std::is_enum_v<dasall::access::AccessDisposition>);
  static_assert(std::is_abstract_v<dasall::access::IAccessGateway>);
  static_assert(std::is_abstract_v<dasall::access::IAccessRuntimeBridge>);
  static_assert(std::is_abstract_v<dasall::access::IProtocolAdapter>);

  const dasall::access::InboundPacket packet{
      .packet_id = "pkt-001",
      .entry_type = "cli",
      .protocol_kind = "uds",
      .peer_ref = "peer://local/operator",
      .payload = "{\"op\":\"ping\"}",
      .async_preferred = false,
      .stream_requested = false,
  };

  const dasall::access::RuntimeDispatchRequest request{
      .packet = packet,
      .async_allowed = true,
      .stream_requested = false,
  };

  dasall::tests::support::assert_equal(
      "pkt-001",
      request.packet.packet_id,
      "access interface surface test should see access supporting types");
  dasall::tests::support::assert_true(
      request.async_allowed,
      "access interface surface test should preserve dispatch request flags");
}

}  // namespace

int main() {
  try {
    access_public_surface_is_discoverable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
