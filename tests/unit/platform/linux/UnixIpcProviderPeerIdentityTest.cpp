#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include <unistd.h>

#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(const std::string& stem)
      : path_(fs::temp_directory_path() /
              (stem + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    fs::create_directories(path_);
  }

  ~ScopedTempDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

void test_unix_ipc_provider_describe_peer_returns_local_identity_for_uds_channel() {
  using dasall::platform::IpcEndpoint;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  UnixIpcProvider provider;
  ScopedTempDirectory temp_root("unix-ipc-provider-peer");

  IpcEndpoint endpoint;
  endpoint.socket_path = (temp_root.path() / "daemon-control.sock").string();

  const auto listener = provider.listen(endpoint, dasall::platform::ListenOptions{});
  assert_true(listener.ok(), "listen should succeed before local uds peer connect");

  const auto channel = provider.connect(endpoint, 10);
  assert_true(channel.ok(), "connect should succeed for local uds endpoint");

  const auto accepted = provider.accept(*listener.value, 10);
  assert_true(accepted.ok(), "accept should succeed before describe_peer");

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
