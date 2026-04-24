#include <exception>
#include <iostream>
#include <string>

#include "RequestValidator.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_injection_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-018-injection";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "{\"query\":\"status\"}";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["x_trace_id"] = "trace-001\r\nX-Evil: 1";
  return request;
}

void rejects_header_injection_attempt() {
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessPublishView;
  using dasall::access::RequestValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPublishView publish_view;
  publish_view.max_payload_bytes = 1024;
  publish_view.max_user_input_bytes = 512;

  RequestValidator validator(publish_view, {"http"});
  const auto result = validator.validate_packet(make_injection_request());

  assert_true(!result.accepted, "header injection candidate should be rejected");
  assert_true(result.error.has_value(), "malformed input should expose access error");
  assert_equal(static_cast<int>(AccessErrorCode::MalformedInput),
               static_cast<int>(result.error->code),
               "header injection should map to MalformedInput");
}

}  // namespace

int main() {
  try {
    rejects_header_injection_attempt();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
