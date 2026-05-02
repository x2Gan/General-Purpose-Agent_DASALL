#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include <sys/stat.h>
#include <unistd.h>

#include "support/TestAssertions.h"
#include "linux/UnixIpcProvider.h"

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

void test_unix_ipc_provider_reports_address_in_use_and_payload_too_large() {
  using dasall::platform::IpcEndpoint;
  using dasall::platform::IpcPayload;
  using dasall::platform::ListenOptions;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  UnixIpcProvider provider;
  ScopedTempDirectory temp_root("unix-ipc-provider-test");

  IpcEndpoint busy_endpoint;
  busy_endpoint.socket_path = (temp_root.path() / "in-use.sock").string();
  ListenOptions listen_options;
  const auto busy_listen = provider.listen(busy_endpoint, listen_options);
  assert_true(!busy_listen.ok(), "listen should fail for in-use endpoint pattern");
  assert_true(busy_listen.error->code == PlatformErrorCode::AddressInUse,
              "in-use endpoint should map to AddressInUse");

  IpcEndpoint normal_endpoint;
  normal_endpoint.socket_path = (temp_root.path() / "ipc.sock").string();
  ListenOptions limited_payload_options;
  limited_payload_options.max_payload_bytes = 4;
  const auto listener = provider.listen(normal_endpoint, limited_payload_options);
  assert_true(listener.ok(), "listen should succeed for normal endpoint");

  const auto accept_timeout = provider.accept(*listener.value, 10);
  assert_true(!accept_timeout.ok(), "accept should time out before peer connection");
  assert_true(accept_timeout.error->code == PlatformErrorCode::Timeout,
              "accept without pending peer should map to Timeout");

  const auto client = provider.connect(normal_endpoint, 10);
  assert_true(client.ok(), "connect should create a pending peer for active listener");

  const auto accepted = provider.accept(*listener.value, 10);
  assert_true(accepted.ok(), "accept should succeed once a peer has connected");

  const IpcPayload oversized_payload{1U, 2U, 3U, 4U, 5U};
  const auto oversized_send = provider.send(*client.value, oversized_payload);
  assert_true(!oversized_send.ok(), "send should fail for payload above max size");
  assert_true(oversized_send.error->code == PlatformErrorCode::PayloadTooLarge,
              "oversized payload should map to PayloadTooLarge");
}

void test_unix_ipc_provider_reports_peer_closed() {
  using dasall::platform::IpcEndpoint;
  using dasall::platform::IpcPayload;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  UnixIpcProvider provider;
  ScopedTempDirectory temp_root("unix-ipc-provider-closed-peer");

  IpcEndpoint endpoint;
  endpoint.socket_path = (temp_root.path() / "closed-peer.sock").string();

  const auto listener = provider.listen(endpoint, dasall::platform::ListenOptions{});
  assert_true(listener.ok(), "listen should succeed for closed peer test endpoint");

  const auto connected = provider.connect(endpoint, 10);
  assert_true(connected.ok(), "connect should succeed for test endpoint");

  const auto accepted = provider.accept(*listener.value, 10);
  assert_true(accepted.ok(), "accept should succeed before close propagation");

  const auto closed = provider.close(*accepted.value);
  assert_true(closed.ok(), "closing accepted server channel should succeed");

  const auto received = provider.receive(*connected.value, 10);
  assert_true(received.ok(), "receive should return peer_closed payload when peer is closed");
  assert_true(received.value->peer_closed, "receive should flag peer_closed=true");

  const IpcPayload payload{9U};
  const auto sent = provider.send(*connected.value, payload);
  assert_true(!sent.ok(), "send should fail when peer is closed");
  assert_true(sent.error->code == PlatformErrorCode::PeerClosed,
              "send on closed peer should map to PeerClosed");
}

void test_unix_ipc_provider_applies_secure_socket_mode_and_unlinks_on_close() {
  using dasall::platform::IpcChannelHandle;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::ListenOptions;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  UnixIpcProvider provider;
  ScopedTempDirectory temp_root("unix-ipc-provider-mode");

  IpcEndpoint endpoint;
  endpoint.socket_path = (temp_root.path() / "mode.sock").string();

  const auto listener = provider.listen(endpoint, ListenOptions{});
  assert_true(listener.ok(), "listen should create a real unix socket for mode checks");

  struct stat socket_stat {};
  const auto stat_result = ::lstat(endpoint.socket_path.c_str(), &socket_stat);
  assert_equal(0, stat_result,
               "listen should materialize a filesystem socket path");
  assert_true(S_ISSOCK(socket_stat.st_mode),
              "listen should create a unix socket entry on disk");
  assert_equal(0600,
               static_cast<int>(socket_stat.st_mode & 0777),
               "listen should apply secure daemon-compatible socket mode");

  const auto close_result = provider.close(
      IpcChannelHandle{.native_fd = listener.value->native_fd});
  assert_true(close_result.ok(),
              "closing a listener handle should succeed through the shared close path");
  assert_true(!fs::exists(endpoint.socket_path),
              "closing a listener handle should unlink the filesystem socket path");
}

}  // namespace

int main() {
  try {
    test_unix_ipc_provider_reports_address_in_use_and_payload_too_large();
    test_unix_ipc_provider_reports_peer_closed();
    test_unix_ipc_provider_applies_secure_socket_mode_and_unlinks_on_close();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}