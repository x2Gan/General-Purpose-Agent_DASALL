#include <exception>
#include <iostream>
#include <string>

#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

void test_decode_request_frame_extracts_v1_fields_and_escapes() {
  using dasall::access::daemon::DaemonAsyncPreference;
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::decode_request_frame;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const std::string payload =
      R"({"schema_version":"1","request_id":"req-030","trace_id":"trace-030",)"
      R"("session_hint":"session-030","idempotency_key":"idem-030","command":"run",)"
      R"("args":{"peer_ref":"cli-user","mode":"safe"},"payload":"hello \"daemon\"",)"
      R"("async_preference":true})";

  const auto decoded = decode_request_frame(payload, 1024U);

  assert_equal(static_cast<int>(DaemonFrameDecodeError::None),
               static_cast<int>(decoded.error),
               "valid daemon frame should decode successfully");
  assert_equal(std::string("1"), decoded.frame.schema_version,
               "codec should preserve schema_version");
  assert_equal(std::string("req-030"), decoded.frame.request_id,
               "codec should extract request_id");
  assert_equal(std::string("trace-030"), decoded.frame.trace_id,
               "codec should extract trace_id");
  assert_true(decoded.frame.session_hint.has_value(),
              "codec should extract session_hint");
  assert_equal(std::string("session-030"), *decoded.frame.session_hint,
               "codec should preserve session_hint");
  assert_true(decoded.frame.idempotency_key.has_value(),
              "codec should extract idempotency_key");
  assert_equal(std::string("idem-030"), *decoded.frame.idempotency_key,
               "codec should preserve idempotency_key");
  assert_equal(std::string("run"), decoded.frame.command,
               "codec should extract command");
  assert_equal(std::string("hello \"daemon\""), decoded.frame.payload,
               "codec should unescape quoted payload text");
  assert_true(decoded.frame.args.contains("peer_ref"),
              "codec should decode args object into string map");
  assert_equal(std::string("cli-user"), decoded.frame.args.at("peer_ref"),
               "codec should preserve args values");
  assert_equal(static_cast<int>(DaemonAsyncPreference::PreferAsync),
               static_cast<int>(decoded.frame.async_preference),
               "codec should project async_preference=true to PreferAsync");
}

void test_encode_response_frame_escapes_user_controlled_fields() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::access::daemon::UdsResponseFrame;
  using dasall::access::daemon::encode_response_frame;
  using dasall::tests::support::assert_true;

  dasall::contracts::AgentResult result;
  result.result_id = "result-030";
  result.response_text = std::string("line1\n\"quoted\"\\tail");
  result.task_completed = true;

  UdsResponseFrame frame;
  frame.request_id = "req-030";
  frame.trace_id = "trace-030";
  frame.session_id = "session-030";
  frame.disposition = UdsResponseDisposition::Completed;
  frame.error_ref = std::string("err:\"quoted\"");
  frame.agent_result = result;

  const std::string encoded = encode_response_frame(frame);

  assert_true(encoded.find("\"schema_version\":\"1\"") != std::string::npos,
              "response frame should include schema_version");
  assert_true(encoded.find("\"disposition\":\"completed\"") != std::string::npos,
              "response frame should encode completed disposition");
  assert_true(encoded.find("line1\\n\\\"quoted\\\"\\\\tail") != std::string::npos,
              "response frame should escape quotes, backslashes and newlines in agent_result");
  assert_true(encoded.find("err:\\\"quoted\\\"") != std::string::npos,
              "response frame should escape quoted error_ref");
}

}  // namespace

int main() {
  try {
    test_decode_request_frame_extracts_v1_fields_and_escapes();
    test_encode_response_frame_escapes_user_controlled_fields();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonFrameCodecTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}