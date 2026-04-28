#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

void assert_decode_error(const std::string& payload,
                         const std::size_t max_payload_bytes,
                         const dasall::access::daemon::DaemonFrameDecodeError expected,
                         const std::string& message) {
  using dasall::access::daemon::decode_request_frame;
  using dasall::tests::support::assert_equal;

  const auto decoded = decode_request_frame(payload, max_payload_bytes);
  assert_equal(static_cast<int>(expected), static_cast<int>(decoded.error), message);
}

void test_missing_schema_version_is_rejected() {
  using dasall::access::daemon::DaemonFrameDecodeError;

  assert_decode_error(
      R"({"command":"ping"})",
      1024U,
      DaemonFrameDecodeError::MissingSchemaVersion,
      "missing schema_version should be rejected before routing");
}

void test_unsupported_schema_version_is_rejected() {
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::map_frame_error_to_publish_envelope;
  using dasall::tests::support::assert_equal;

  assert_decode_error(
      R"({"schema_version":"2","command":"ping"})",
      1024U,
      DaemonFrameDecodeError::UnsupportedSchemaVersion,
      "unsupported schema_version should be rejected");

  const auto envelope = map_frame_error_to_publish_envelope(
      DaemonFrameDecodeError::UnsupportedSchemaVersion,
      "req-unsupported",
      "trace-unsupported");
  assert_equal(std::string("426"), envelope.protocol_status_hint,
               "unsupported schema version should map to upgrade-required semantics");
}

void test_unknown_command_is_rejected() {
  using dasall::access::daemon::DaemonFrameDecodeError;

  assert_decode_error(
      R"({"schema_version":"1","command":"tail-logs"})",
      1024U,
      DaemonFrameDecodeError::UnknownCommand,
      "unknown command should fail closed");
}

void test_payload_too_large_is_rejected() {
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::map_frame_error_to_publish_envelope;
  using dasall::tests::support::assert_equal;

  const std::string oversized =
      R"({"schema_version":"1","command":"run","payload":"1234567890"})";

  assert_decode_error(
      oversized,
      16U,
      DaemonFrameDecodeError::PayloadTooLarge,
      "oversized payload should be rejected by codec limit");

  const auto envelope = map_frame_error_to_publish_envelope(
      DaemonFrameDecodeError::PayloadTooLarge,
      "req-oversized",
      "trace-oversized");
  assert_equal(std::string("413"), envelope.protocol_status_hint,
               "payload-too-large should map to 413 semantics");
}

void test_non_utf8_payload_is_rejected() {
  using dasall::access::daemon::DaemonFrameDecodeError;

  std::string invalid_utf8 =
      "{\"schema_version\":\"1\",\"command\":\"run\",\"payload\":\"bad";
  invalid_utf8.push_back(static_cast<char>(0xC3));
  invalid_utf8.push_back(static_cast<char>(0x28));
  invalid_utf8 += "\"}";

  assert_decode_error(
      invalid_utf8,
      1024U,
      DaemonFrameDecodeError::MalformedEnvelope,
      "non UTF-8 payload should be rejected as malformed");
}

void test_truncated_payload_is_rejected() {
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::map_frame_error_to_publish_envelope;
  using dasall::tests::support::assert_equal;

  assert_decode_error(
      R"({"schema_version":"1","command":"run","payload":"unterminated")",
      1024U,
      DaemonFrameDecodeError::MalformedEnvelope,
      "truncated payload should not be parsed as a valid frame");

  const auto envelope = map_frame_error_to_publish_envelope(
      DaemonFrameDecodeError::MalformedEnvelope,
      "req-malformed",
      "trace-malformed");
  assert_equal(std::string("malformed_envelope"), envelope.payload,
               "malformed error should project stable reason code");
}

}  // namespace

int main() {
  try {
    test_missing_schema_version_is_rejected();
    test_unsupported_schema_version_is_rejected();
    test_unknown_command_is_rejected();
    test_payload_too_large_is_rejected();
    test_non_utf8_payload_is_rejected();
    test_truncated_payload_is_rejected();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonFrameCodecMalformedTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}