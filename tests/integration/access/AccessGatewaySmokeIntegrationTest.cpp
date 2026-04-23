#include <exception>
#include <iostream>
#include <type_traits>

#include "AccessTypes.h"
#include "IAccessGateway.h"
#include "support/TestAssertions.h"

namespace {

void access_smoke_topology_is_discoverable() {
  static_assert(std::is_abstract_v<dasall::access::IAccessGateway>);

  const dasall::access::PublishEnvelope envelope{
      .request_id = "req-001",
      .result_id = "result-001",
      .payload = "{\"status\":\"ok\"}",
      .protocol_kind = "cli",
  };

  dasall::tests::support::assert_equal(
      "req-001",
      envelope.request_id,
      "access integration smoke skeleton should preserve publish envelope fields");
}

}  // namespace

int main() {
  try {
    access_smoke_topology_is_discoverable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
