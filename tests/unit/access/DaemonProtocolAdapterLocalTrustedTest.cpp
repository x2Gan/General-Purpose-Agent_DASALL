#include <exception>
#include <iostream>
#include <memory>

#include "daemon/DaemonProtocolAdapter.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

void test_daemon_protocol_adapter_projects_local_peer_fact_for_trusted_path() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/local-control.sock";

  const auto channel = ipc->connect(endpoint, 10);
  assert_true(channel.ok(), "connect should provide a local channel for trusted test");

  const auto fact = adapter.describe_local_peer_uid_fact(*channel.value, "actor://local/operator");
  assert_equal(std::string("actor://local/operator"), fact.actor_ref,
               "adapter should preserve actor_ref in local peer fact");
  assert_true(fact.is_local_socket_peer,
              "local socket should be marked as local peer");
  assert_true(fact.peer_uid != 0U,
              "local trusted fact should expose non-zero uid");
  assert_true(fact.eligible_for_local_trusted,
              "local socket peer with non-zero uid should be eligible for local trusted");
}

void test_daemon_protocol_adapter_marks_remote_peer_as_not_trusted() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/remote-control.sock";

  const auto channel = ipc->connect(endpoint, 10);
  assert_true(channel.ok(), "connect should succeed for remote-marked endpoint");

  const auto fact = adapter.describe_local_peer_uid_fact(*channel.value, "actor://remote/operator");
  assert_true(!fact.is_local_socket_peer,
              "remote endpoint should not be treated as local socket peer");
  assert_true(!fact.eligible_for_local_trusted,
              "remote endpoint should not be eligible for local trusted");
}

}  // namespace

int main() {
  try {
    test_daemon_protocol_adapter_projects_local_peer_fact_for_trusted_path();
    test_daemon_protocol_adapter_marks_remote_peer_as_not_trusted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
