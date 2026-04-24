#include <exception>
#include <iostream>
#include <string>

#include "RequestValidator.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_payload_limit_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-018-limit";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = std::string(64, 'x');
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  return request;
}

void rejects_payload_when_size_exceeds_limit() {
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessPublishView;
  using dasall::access::RequestValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPublishView publish_view;
  publish_view.max_payload_bytes = 16;
  publish_view.max_user_input_bytes = 256;

  RequestValidator validator(publish_view, {"http"});
  const auto result = validator.validate_packet(make_payload_limit_request());

  assert_true(!result.accepted, "oversized payload should be rejected");
  assert_true(result.error.has_value(), "oversized payload should expose access error");
  assert_equal(static_cast<int>(AccessErrorCode::PayloadTooLarge),
               static_cast<int>(result.error->code),
               "payload limit rejection should map to PayloadTooLarge");
}

}  // namespace

int main() {
  try {
    rejects_payload_when_size_exceeds_limit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
