#include <exception>
#include <iostream>
#include <memory>

#include "CliIpcClient.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

void test_cli_daemon_ping_path_is_routed_via_ipc_uds() {
  using dasall::apps::cli::CliIpcClient;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();

  IpcEndpoint daemon_endpoint;
  daemon_endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  const CliIpcClient client(ipc, daemon_endpoint, 10);
  assert_true(client.ping_daemon(),
              "cli daemon ping integration should pass through ipc uds path");
}

}  // namespace

int main() {
  try {
    test_cli_daemon_ping_path_is_routed_via_ipc_uds();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
