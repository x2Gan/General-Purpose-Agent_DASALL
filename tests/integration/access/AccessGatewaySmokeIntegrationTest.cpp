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
      .session_id = "session-001",
      .trace_id = "trace-001",
      .channel_ref = "channel://cli/local",
      .protocol_kind = "cli",
      .agent_result = {},
      .protocol_status_hint = "200",
      .protocol_metadata = "{}",
      .is_final = true,
      .payload = "{\"status\":\"ok\"}",
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
