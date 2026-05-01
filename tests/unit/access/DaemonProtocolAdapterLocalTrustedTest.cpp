#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include <unistd.h>

#include "PlatformResult.h"
#include "daemon/DaemonProtocolAdapter.h"
#include "error/ResultCode.h"
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

class FailingDescribePeerIpc final : public dasall::platform::IIPC {
 public:
  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle>::success(
        dasall::platform::IpcListenerHandle{.native_fd = 1U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 2U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 3U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = 0U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult>::success(
        dasall::platform::IpcReceiveResult{});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>::failure(
        dasall::platform::PlatformError{
            .code = dasall::platform::PlatformErrorCode::NotFound,
            .category = dasall::platform::PlatformErrorCategory::IPC,
            .retryable_hint = false,
            .syscall_name = "getsockopt",
            .errno_value = 2,
            .detail = "peer identity unavailable",
        });
  }

  [[nodiscard]] dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

void test_daemon_protocol_adapter_projects_local_peer_fact_for_trusted_path() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  DaemonProtocolAdapter adapter(ipc);
  ScopedTempDirectory temp_root("daemon-local-trusted");

  IpcEndpoint endpoint;
  endpoint.socket_path = (temp_root.path() / "local-control.sock").string();

  const auto listener = ipc->listen(endpoint, dasall::platform::ListenOptions{});
  assert_true(listener.ok(), "listen should succeed before trusted local peer connect");

  const auto channel = ipc->connect(endpoint, 10);
  assert_true(channel.ok(), "connect should provide a local channel for trusted test");

  const auto accepted = ipc->accept(*listener.value, 10);
  assert_true(accepted.ok(), "accept should succeed before trusted local peer projection");

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
  ScopedTempDirectory temp_root("daemon-remote-trusted");

  IpcEndpoint endpoint;
  endpoint.socket_path = (temp_root.path() / "remote-control.sock").string();

  const auto listener = ipc->listen(endpoint, dasall::platform::ListenOptions{});
  assert_true(listener.ok(), "listen should succeed before remote-marked peer connect");

  const auto channel = ipc->connect(endpoint, 10);
  assert_true(channel.ok(), "connect should succeed for remote-marked endpoint");

  const auto accepted = ipc->accept(*listener.value, 10);
  assert_true(accepted.ok(), "accept should succeed before remote-marked peer projection");

  const auto fact = adapter.describe_local_peer_uid_fact(*channel.value, "actor://remote/operator");
  assert_true(!fact.is_local_socket_peer,
              "remote endpoint should not be treated as local socket peer");
  assert_true(!fact.eligible_for_local_trusted,
              "remote endpoint should not be eligible for local trusted");
}

void test_daemon_protocol_adapter_describe_peer_failure_is_fail_closed() {
  using dasall::access::daemon::DaemonProtocolAdapter;
  using dasall::platform::IpcChannelHandle;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<FailingDescribePeerIpc>();
  DaemonProtocolAdapter adapter(ipc);

  IpcChannelHandle channel;
  channel.native_fd = 33U;

  const auto fact = adapter.describe_local_peer_uid_fact(
      channel, "actor://daemon/local-missing-peer");
  assert_equal(std::string("actor://daemon/local-missing-peer"), fact.actor_ref,
               "describe_peer failure path should preserve actor_ref");
  assert_true(!fact.is_local_socket_peer,
              "describe_peer failure should not infer local socket peer");
  assert_true(!fact.eligible_for_local_trusted,
              "describe_peer failure should fail closed for local trusted eligibility");
}

}  // namespace

int main() {
  try {
    test_daemon_protocol_adapter_projects_local_peer_fact_for_trusted_path();
    test_daemon_protocol_adapter_marks_remote_peer_as_not_trusted();
    test_daemon_protocol_adapter_describe_peer_failure_is_fail_closed();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
