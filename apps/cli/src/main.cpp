#include <iostream>
#include <memory>

#include "CliIpcClient.h"
#include "linux/UnixIpcProvider.h"

int main() {
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();

  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  const dasall::apps::cli::CliIpcClient client(ipc, endpoint);
  const bool ping_ok = client.ping_daemon();

  std::cout << "dasall_cli uds-ping " << (ping_ok ? "ok" : "failed") << std::endl;
  return ping_ok ? 0 : 1;
}
