#include <exception>
#include <iostream>
#include <memory>

#include "CliIpcClient.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

/// 验证 connect_deadline_ms == 0 时 ping_daemon() 返回 false
/// （UnixIpcProvider stub：deadline=0 模拟连接超时，对应真实 daemon 不可达场景）
void test_cli_ipc_client_ping_fails_on_timeout() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  // deadline=0 触发 stub 的 timeout 返回路径（模拟不可达）
  const CliIpcClient client(ipc, endpoint, 0);
  assert_true(!client.ping_daemon(),
              "cli ipc client should fail closed when connect deadline is zero "
              "(simulates daemon unavailable)");
}

/// 验证 socket 路径为空时 send_payload() 返回 false（endpoint 无效）
void test_cli_ipc_client_send_fails_for_empty_path() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  IpcEndpoint endpoint;
  endpoint.socket_path.clear();

  const CliIpcClient client(ipc, endpoint, 10);
  assert_true(!client.send_payload(R"({"op":"submit"})"),
              "cli ipc client should fail closed for empty socket path");
}

/// 验证 null ipc 时 send_payload() 返回 false（防御性检查）
void test_cli_ipc_client_send_fails_for_null_ipc() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::tests::support::assert_true;

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-test.sock";

  const CliIpcClient client(nullptr, endpoint, 10);
  assert_true(!client.send_payload("{}"),
              "cli ipc client should fail closed when ipc is null");
}

}  // namespace

int main() {
  try {
    test_cli_ipc_client_ping_fails_on_timeout();
    test_cli_ipc_client_send_fails_for_empty_path();
    test_cli_ipc_client_send_fails_for_null_ipc();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
