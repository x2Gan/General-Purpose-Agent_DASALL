#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "CliIpcClient.h"
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

class ScriptedIpc final : public dasall::platform::IIPC {
 public:
  std::string response_text;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "listen unused in cli response tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "accept unused in cli response tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 73U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
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
                            "describe_peer unused in cli response tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

std::string make_response(
    const dasall::access::daemon::UdsResponseDisposition disposition,
    std::optional<std::string> receipt_ref,
    std::optional<std::string> error_ref,
    std::optional<std::string> response_text) {
  using dasall::access::daemon::UdsResponseFrame;
  using dasall::access::daemon::encode_response_frame;

  UdsResponseFrame frame;
  frame.request_id = "cli-run";
  frame.trace_id = "cli-run-trace";
  frame.disposition = disposition;
  frame.receipt_ref = std::move(receipt_ref);
  frame.error_ref = std::move(error_ref);
  if (response_text.has_value()) {
    dasall::contracts::AgentResult agent_result;
    agent_result.response_text = *response_text;
    agent_result.task_completed = disposition ==
                                  dasall::access::daemon::UdsResponseDisposition::Completed;
    frame.agent_result = std::move(agent_result);
  }
  return encode_response_frame(frame);
}

void test_cli_ipc_client_submit_surfaces_accepted_async_receipt() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->response_text = make_response(
      UdsResponseDisposition::AcceptedAsync,
      std::string("receipt-031"),
      std::nullopt,
      std::string("queued"));

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response = client.submit(R"({"input":"hello"})");

  assert_true(response.ok(),
              "cli submit should parse accepted_async daemon response");
  assert_true(response.is_accepted_async(),
              "cli submit should distinguish accepted_async disposition");
  assert_true(response.receipt_ref.has_value(),
              "cli submit should surface receipt_ref for accepted_async response");
  assert_equal(std::string("receipt-031"), *response.receipt_ref,
               "cli submit should preserve receipt_ref");
}

void test_cli_ipc_client_readiness_surfaces_not_ready_disposition() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->response_text = make_response(
      UdsResponseDisposition::NotReady,
      std::nullopt,
      std::string("runtime_bridge_unreachable"),
      std::string("NOT_READY"));

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response = client.read_readiness();

  assert_true(response.ok(),
              "cli readiness should parse daemon not_ready response");
  assert_true(response.is_not_ready(),
              "cli readiness should distinguish not_ready disposition");
  assert_true(response.error_ref.has_value(),
              "cli readiness should preserve error_ref for not_ready response");
}

void test_cli_ipc_client_status_surfaces_rejected_error() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->response_text = make_response(
      UdsResponseDisposition::Rejected,
      std::nullopt,
      std::string("authorization_denied"),
      std::string("rejected"));

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response = client.query_status("receipt-031", "owner-token");

  assert_true(response.ok(),
              "cli status should parse daemon rejected response envelope");
  assert_true(!response.is_completed() && !response.is_accepted_async() &&
                  !response.is_not_ready(),
              "cli status should leave rejected disposition distinguishable from success paths");
  assert_true(response.error_ref.has_value(),
              "cli status should surface error_ref for rejected response");
  assert_equal(std::string("authorization_denied"), *response.error_ref,
               "cli status should preserve rejected error_ref");
}

}  // namespace

int main() {
  try {
    test_cli_ipc_client_submit_surfaces_accepted_async_receipt();
    test_cli_ipc_client_readiness_surfaces_not_ready_disposition();
    test_cli_ipc_client_status_surfaces_rejected_error();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}