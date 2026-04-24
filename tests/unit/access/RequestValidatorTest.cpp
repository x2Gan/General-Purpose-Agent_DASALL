#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "RequestValidator.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_valid_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-018-valid";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.packet.payload = "{\"query\":\"health\"}";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["x_trace_id"] = "trace-001";
  return request;
}

void accepts_valid_request_with_allowed_protocol() {
  using dasall::access::AccessPublishView;
  using dasall::access::RequestValidator;
  using dasall::tests::support::assert_true;

  AccessPublishView publish_view;
  publish_view.max_payload_bytes = 1024;
  publish_view.max_user_input_bytes = 256;

  RequestValidator validator(publish_view, std::vector<std::string>{"http", "ipc"});
  const auto result = validator.validate_packet(make_valid_request());

  assert_true(result.accepted, "validator should accept well-formed request");
  assert_true(!result.error.has_value(), "accepted request should not carry validation error");
}

void rejects_unknown_protocol_kind() {
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessPublishView;
  using dasall::access::RequestValidator;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessPublishView publish_view;
  publish_view.max_payload_bytes = 1024;
  publish_view.max_user_input_bytes = 256;

  auto request = make_valid_request();
  request.packet.protocol_kind = "mqtt";

  RequestValidator validator(publish_view, std::vector<std::string>{"http", "ipc"});
  const auto result = validator.validate_packet(request);

  assert_true(!result.accepted, "validator should reject protocol out of allow set");
  assert_true(result.error.has_value(), "rejected request should expose access error");
  assert_equal(static_cast<int>(AccessErrorCode::UnsupportedProtocol),
               static_cast<int>(result.error->code),
               "protocol rejection should map to UnsupportedProtocol");
}

}  // namespace

int main() {
  try {
    accepts_valid_request_with_allowed_protocol();
    rejects_unknown_protocol_kind();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
