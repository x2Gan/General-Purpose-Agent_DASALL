#include <exception>
#include <iostream>
#include <memory>

#include "CliIpcClient.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

void test_cli_ipc_client_ping_succeeds_for_valid_uds_endpoint() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/cli-daemon.sock";

  const CliIpcClient client(ipc, endpoint, 10);
  assert_true(client.ping_daemon(),
              "cli ipc client ping should succeed for valid local uds endpoint");
}

void test_cli_ipc_client_send_fails_for_invalid_endpoint() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  IpcEndpoint endpoint;
  endpoint.socket_path.clear();

  const CliIpcClient client(ipc, endpoint, 10);
  assert_true(!client.send_payload("{}"),
              "cli ipc client should fail closed when uds endpoint is invalid");
}

}  // namespace

int main() {
  try {
    test_cli_ipc_client_ping_succeeds_for_valid_uds_endpoint();
    test_cli_ipc_client_send_fails_for_invalid_endpoint();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
