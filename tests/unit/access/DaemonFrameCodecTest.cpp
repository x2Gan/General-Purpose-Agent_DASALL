#include <exception>
#include <iostream>
#include <string>

#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

void test_decode_request_frame_extracts_v1_fields_and_escapes() {
  using dasall::access::daemon::DaemonAsyncPreference;
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::DaemonOutputMode;
  using dasall::access::daemon::decode_request_frame;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const std::string payload =
      R"({"schema_version":"1","request_id":"req-030","trace_id":"trace-030",)"
      R"("session_hint":"session-030","idempotency_key":"idem-030","command":"run",)"
      R"("args":{"peer_ref":"cli-user","mode":"safe"},"payload":"hello \"daemon\"",)"
      R"("async_preference":true,"output_mode":"json","deadline_ms":1500})";

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
  assert_equal(static_cast<int>(DaemonOutputMode::Json),
               static_cast<int>(decoded.frame.output_mode),
               "codec should decode output_mode=json into request frame");
  assert_true(decoded.frame.deadline_ms.has_value(),
              "codec should decode deadline_ms when provided");
  assert_equal(1500, *decoded.frame.deadline_ms,
               "codec should preserve deadline_ms value");
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

void test_encode_request_frame_writes_v1_cli_envelope() {
  using dasall::access::daemon::DaemonAsyncPreference;
  using dasall::access::daemon::DaemonOutputMode;
  using dasall::access::daemon::UdsRequestFrame;
  using dasall::access::daemon::encode_request_frame;
  using dasall::tests::support::assert_true;

  UdsRequestFrame frame;
  frame.request_id = "req-031";
  frame.trace_id = "trace-031";
  frame.command = "status";
  frame.args.emplace("receipt_ref", "receipt-031");
  frame.args.emplace("ownership_token", "owner-token");
  frame.payload = "";
  frame.async_preference = DaemonAsyncPreference::PreferSync;
  frame.output_mode = DaemonOutputMode::Json;
  frame.deadline_ms = 900;

  const std::string encoded = encode_request_frame(frame);

  assert_true(encoded.find("\"schema_version\":\"1\"") != std::string::npos,
              "request frame should include schema version");
  assert_true(encoded.find("\"command\":\"status\"") != std::string::npos,
              "request frame should include command");
  assert_true(encoded.find("\"receipt_ref\":\"receipt-031\"") != std::string::npos,
              "request frame should encode args map entries");
  assert_true(encoded.find("\"async_preference\":false") != std::string::npos,
              "request frame should encode sync preference as false");
  assert_true(encoded.find("\"output_mode\":\"json\"") != std::string::npos,
              "request frame should encode output mode as stable string value");
  assert_true(encoded.find("\"deadline_ms\":900") != std::string::npos,
              "request frame should encode deadline_ms when provided");
}

void test_decode_request_frame_accepts_knowledge_command() {
  using dasall::access::daemon::DaemonCommandKind;
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::decode_request_frame;
  using dasall::tests::support::assert_equal;

  const std::string payload =
      R"({"schema_version":"1","request_id":"req-knowledge","trace_id":"trace-knowledge",)"
      R"("command":"knowledge","args":{"operation":"retrieve"},)"
      R"("payload":"operation=retrieve;query_text=DeepSeek Chat",)"
      R"("async_preference":false,"output_mode":"json"})";

  const auto decoded = decode_request_frame(payload, 1024U);

  assert_equal(static_cast<int>(DaemonFrameDecodeError::None),
               static_cast<int>(decoded.error),
               "knowledge command should decode as a known daemon command");
  assert_equal(static_cast<int>(DaemonCommandKind::Knowledge),
               static_cast<int>(decoded.frame.command_kind()),
               "knowledge frame should expose Knowledge command kind");
  assert_equal(std::string("operation=retrieve;query_text=DeepSeek Chat"),
               decoded.frame.payload,
               "knowledge frame should preserve operation payload");
}

void test_decode_response_frame_extracts_disposition_receipt_and_payload() {
  using dasall::access::daemon::DaemonFrameDecodeError;
  using dasall::access::daemon::DecodedDaemonResponseFrame;
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::access::daemon::UdsResponseFrame;
  using dasall::access::daemon::decode_response_frame;
  using dasall::access::daemon::encode_response_frame;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::contracts::AgentResult agent_result;
  agent_result.result_id = "result-031";
  agent_result.response_text = std::string("pong {\"readiness\":\"READY\"}");
  agent_result.task_completed = true;

  UdsResponseFrame frame;
  frame.request_id = "req-031";
  frame.trace_id = "trace-031";
  frame.disposition = UdsResponseDisposition::Completed;
  frame.exit_code_hint = 200;
  frame.receipt_ref = "receipt-031";
  frame.agent_result = agent_result;

  const std::string encoded = encode_response_frame(frame);
  const DecodedDaemonResponseFrame decoded = decode_response_frame(encoded, 1024U);

  assert_equal(static_cast<int>(DaemonFrameDecodeError::None),
               static_cast<int>(decoded.error),
               "encoded response frame should round-trip through decoder");
  assert_equal(static_cast<int>(UdsResponseDisposition::Completed),
               static_cast<int>(decoded.frame.disposition),
               "response decoder should preserve disposition");
  assert_true(decoded.frame.receipt_ref.has_value(),
              "response decoder should extract receipt_ref");
  assert_equal(std::string("receipt-031"), *decoded.frame.receipt_ref,
               "response decoder should preserve receipt_ref");
  assert_true(decoded.frame.exit_code_hint.has_value(),
              "response decoder should extract exit_code_hint");
  assert_equal(200, *decoded.frame.exit_code_hint,
               "response decoder should preserve exit_code_hint");
  assert_true(decoded.frame.agent_result.has_value(),
              "response decoder should extract nested agent_result");
  assert_equal(std::string("pong {\"readiness\":\"READY\"}"),
               *decoded.frame.agent_result->response_text,
               "response decoder should preserve escaped response_text");
}

}  // namespace

int main() {
  try {
    test_decode_request_frame_extracts_v1_fields_and_escapes();
    test_encode_response_frame_escapes_user_controlled_fields();
    test_encode_request_frame_writes_v1_cli_envelope();
    test_decode_request_frame_accepts_knowledge_command();
    test_decode_response_frame_extracts_disposition_receipt_and_payload();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonFrameCodecTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}