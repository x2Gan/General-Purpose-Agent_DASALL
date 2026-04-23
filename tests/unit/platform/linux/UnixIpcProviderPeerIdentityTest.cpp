#include <exception>
#include <iostream>

#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

void test_unix_ipc_provider_describe_peer_returns_local_identity_for_uds_channel() {
  using dasall::platform::IpcEndpoint;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  UnixIpcProvider provider;

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/daemon-control.sock";

  const auto channel = provider.connect(endpoint, 10);
  assert_true(channel.ok(), "connect should succeed for local uds endpoint");

  const auto peer = provider.describe_peer(*channel.value);
  assert_true(peer.ok(), "describe_peer should succeed for active channel");
  assert_true(peer.value->is_local_socket_peer,
              "local uds endpoint should be tagged as local socket peer");
  assert_true(peer.value->peer_uid != 0U,
              "local peer identity should expose non-zero uid");
  assert_true(peer.value->has_consistent_values(),
              "peer identity snapshot should remain internally consistent");

  const dasall::platform::IpcChannelHandle invalid_handle{};
  const auto invalid = provider.describe_peer(invalid_handle);
  assert_true(!invalid.ok(), "describe_peer should reject invalid channel handle");
  assert_true(invalid.error->code == PlatformErrorCode::InvalidArgument,
              "invalid handle should map to InvalidArgument");
}

}  // namespace

int main() {
  try {
    test_unix_ipc_provider_describe_peer_returns_local_identity_for_uds_channel();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
