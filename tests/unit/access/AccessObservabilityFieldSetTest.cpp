#include <exception>
#include <iostream>
#include <string>

#include "AccessObservabilityBridge.h"
#include "support/TestAssertions.h"

namespace {

void emit_publish_failed_contains_required_field_set() {
  using dasall::access::AccessErrorCode;
  using dasall::access::AccessObservabilityBridge;
  using dasall::access::AccessObservabilityEvent;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessObservabilityEvent captured;
  bool invoked = false;
  AccessObservabilityBridge bridge(
      [&captured, &invoked](const AccessObservabilityEvent& event) {
        invoked = true;
        captured = event;
        return false;
      });

  dasall::access::PublishEnvelope envelope;
  envelope.request_id = "req-023-fields";
  envelope.session_id = "sess-023-fields";
  envelope.trace_id = "trace-023-fields";
  envelope.result_id = "result-023-fields";
  envelope.protocol_kind = "http";

  const bool emitted = bridge.emit_publish_failed(
      envelope,
      AccessErrorCode::PublishChannelUnavailable,
      "socket_write_failed");

  assert_true(!emitted,
              "bridge should return backend result so caller can observe emission failure");
  assert_true(invoked, "bridge should invoke backend even when backend returns false");

  assert_equal(std::string("access.publish.failed"), captured.name,
               "event name should be publish_failed");
  assert_true(captured.fields.contains("request_id"), "field set should contain request_id");
  assert_true(captured.fields.contains("session_id"), "field set should contain session_id");
  assert_true(captured.fields.contains("trace_id"), "field set should contain trace_id");
  assert_true(captured.fields.contains("result_id"), "field set should contain result_id");
  assert_true(captured.fields.contains("error_code"), "field set should contain error_code");
  assert_equal(std::string("req-023-fields"), captured.fields.at("request_id"),
               "request_id should be preserved");
  assert_equal(std::to_string(static_cast<int>(AccessErrorCode::PublishChannelUnavailable)),
               captured.fields.at("error_code"),
               "error_code should be encoded using AccessErrorCode numeric value");
}

}  // namespace

int main() {
  try {
    emit_publish_failed_contains_required_field_set();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
