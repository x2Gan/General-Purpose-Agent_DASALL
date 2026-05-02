#include <exception>
#include <iostream>
#include <memory>

#include "CliIpcClient.h"
#include "daemon/DaemonEndpointDefaults.h"
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

class FailingConnectIpc final : public dasall::platform::IIPC {
 public:
  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "listen unused in cli unavailable tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "accept unused in cli unavailable tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::Timeout,
                            dasall::platform::PlatformErrorCategory::IPC,
                            "daemon unavailable"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcSendResult>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InternalFailure,
                            dasall::platform::PlatformErrorCategory::Internal,
                            "send should not be called after connect failure"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InternalFailure,
                            dasall::platform::PlatformErrorCategory::Internal,
                            "receive should not be called after connect failure"));
  }

  dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot> describe_peer(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>::failure(
        make_platform_error(dasall::platform::PlatformErrorCode::InvalidArgument,
                            dasall::platform::PlatformErrorCategory::Validation,
                            "describe_peer unused in cli unavailable tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

void test_cli_ipc_client_ping_fails_for_invalid_configuration() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<FailingConnectIpc>();
  IpcEndpoint endpoint;
  endpoint.socket_path.clear();

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response = client.ping_daemon();
  assert_true(!response.ok(),
              "cli ipc client should fail closed for empty socket path");
  assert_true(response.failure_reason.find("invalid cli ipc client configuration") !=
                  std::string::npos,
              "invalid configuration should surface a stable failure reason");
}

void test_cli_ipc_client_ping_fails_when_daemon_is_unavailable() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<FailingConnectIpc>();
  IpcEndpoint endpoint;
  endpoint.socket_path = dasall::access::daemon::kDefaultDaemonSocketPath;

  const CliIpcClient client(ipc, endpoint, 10);
  const auto response = client.ping_daemon();
  assert_true(!response.ok(),
              "cli ipc client should fail closed when daemon connect fails");
  assert_true(response.failure_reason.find("daemon unavailable") !=
                  std::string::npos,
              "connect failure should be reported back to CLI caller");
}

void test_cli_ipc_client_send_fails_for_null_ipc() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_true;

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-test.sock";

  const CliIpcClient client(nullptr, endpoint, 10);
  const auto response = client.submit("{}");
  assert_true(!response.ok(),
              "cli ipc client should fail closed when ipc is null");
}

}  // namespace

int main() {
  try {
    test_cli_ipc_client_ping_fails_for_invalid_configuration();
    test_cli_ipc_client_ping_fails_when_daemon_is_unavailable();
    test_cli_ipc_client_send_fails_for_null_ipc();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
