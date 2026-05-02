#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "CliIpcClient.h"
#include "daemon/DaemonEndpointDefaults.h"
#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

dasall::platform::PlatformError make_platform_error(
    const dasall::platform::PlatformErrorCode code,
    const dasall::platform::PlatformErrorCategory category,
    std::string detail) {
  return dasall::platform::PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = "unit-test",
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

dasall::platform::IpcPayload make_payload(std::string_view text) {
  dasall::platform::IpcPayload payload;
  payload.reserve(text.size());
  for (const char ch : text) {
    payload.push_back(static_cast<std::uint8_t>(ch));
  }
  return payload;
}

std::string make_completed_response(std::string_view request_id,
                                    std::string_view response_text) {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::access::daemon::UdsResponseFrame;
  using dasall::access::daemon::encode_response_frame;

  dasall::contracts::AgentResult agent_result;
  agent_result.response_text = std::string(response_text);
  agent_result.task_completed = true;

  UdsResponseFrame frame;
  frame.request_id = std::string(request_id);
  frame.trace_id = frame.request_id + "-trace";
  frame.disposition = UdsResponseDisposition::Completed;
  frame.agent_result = agent_result;
  return encode_response_frame(frame);
}

class ScriptedIpc final : public dasall::platform::IIPC {
 public:
  std::string response_text;
  std::string last_sent_payload;
  bool close_called = false;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "listen unused in cli ipc client unit tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "accept unused in cli ipc client unit tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 41U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    last_sent_payload.assign(reinterpret_cast<const char*>(payload.data()),
                             payload.size());
    return dasall::platform::PlatformResult<dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    dasall::platform::IpcReceiveResult result;
    result.data = make_payload(response_text);
    return dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult>::success(
        result);
  }

  dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot> describe_peer(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "describe_peer unused in cli ipc client unit tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    close_called = true;
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

void test_cli_ipc_client_ping_encodes_v1_request_and_parses_response() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->response_text = make_completed_response("cli-ping", "pong READY");

  IpcEndpoint endpoint;
  endpoint.socket_path = dasall::access::daemon::kDefaultDaemonSocketPath;

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response = client.ping_daemon();

  assert_true(response.ok(),
              "cli ipc client ping should parse a completed daemon response");
  assert_true(response.is_completed(),
              "cli ipc client ping should surface completed disposition");
  assert_true(response.response_text.has_value(),
              "cli ipc client ping should surface response text");
  assert_equal(std::string("pong READY"), *response.response_text,
               "cli ipc client ping should preserve daemon response text");
  assert_true(ipc->last_sent_payload.find("\"schema_version\":\"1\"") !=
                  std::string::npos,
              "cli ipc client should encode schema_version in request frame");
  assert_true(ipc->last_sent_payload.find("\"command\":\"ping\"") !=
                  std::string::npos,
              "cli ipc client should encode ping command in request frame");
  assert_true(ipc->close_called,
              "cli ipc client should close the ipc channel after roundtrip");
}

void test_cli_ipc_client_status_encodes_receipt_owner_arguments() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->response_text = make_completed_response("cli-status", "completed");

  IpcEndpoint endpoint;
  endpoint.socket_path = dasall::access::daemon::kDefaultDaemonSocketPath;

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response =
      client.query_status("receipt-031", "ownership-token", "local://uid/1000");

  assert_true(response.ok(),
              "cli ipc client status should parse daemon response for receipt query");
  assert_true(ipc->last_sent_payload.find("\"command\":\"status\"") !=
                  std::string::npos,
              "status request should encode daemon status command");
  assert_true(ipc->last_sent_payload.find("\"receipt_ref\":\"receipt-031\"") !=
                  std::string::npos,
              "status request should encode receipt_ref inside args");
  assert_true(
      ipc->last_sent_payload.find("\"ownership_token\":\"ownership-token\"") !=
          std::string::npos,
      "status request should encode ownership token inside args");
  assert_true(ipc->last_sent_payload.find("\"actor_ref\":\"local://uid/1000\"") !=
                  std::string::npos,
              "status request should encode actor_ref when explicitly provided");
}

}  // namespace

int main() {
  try {
    test_cli_ipc_client_ping_encodes_v1_request_and_parses_response();
    test_cli_ipc_client_status_encodes_receipt_owner_arguments();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
