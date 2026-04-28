#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "DaemonListenerHost.h"
#include "PlatformError.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::apps::daemon::DaemonListenerHost;
using dasall::platform::IpcChannelHandle;
using dasall::platform::IpcEndpoint;
using dasall::platform::IpcListenerHandle;
using dasall::platform::IpcPayload;
using dasall::platform::IpcReceiveResult;
using dasall::platform::IpcSendResult;
using dasall::platform::ListenOptions;
using dasall::platform::PeerIdentitySnapshot;
using dasall::platform::PlatformError;
using dasall::platform::PlatformErrorCategory;
using dasall::platform::PlatformErrorCode;
using dasall::platform::PlatformResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                       PlatformErrorCategory category,
                                       std::string detail) {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string_view stem)
      : path_(fs::temp_directory_path() /
              (std::string(stem) + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    fs::create_directories(path_);
    fs::permissions(path_, static_cast<fs::perms>(0700), fs::perm_options::replace);
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

class NativeUnixSocket {
 public:
  NativeUnixSocket() = default;

  NativeUnixSocket(int fd, fs::path path)
      : fd_(fd), path_(std::move(path)) {}

  NativeUnixSocket(const NativeUnixSocket&) = delete;
  NativeUnixSocket& operator=(const NativeUnixSocket&) = delete;

  NativeUnixSocket(NativeUnixSocket&& other) noexcept
      : fd_(other.fd_), path_(std::move(other.path_)) {
    other.fd_ = -1;
  }

  NativeUnixSocket& operator=(NativeUnixSocket&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      path_ = std::move(other.path_);
      other.fd_ = -1;
    }

    return *this;
  }

  ~NativeUnixSocket() {
    reset();
  }

  void close_keep_path() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  void reset() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }

    if (!path_.empty()) {
      std::error_code error;
      fs::remove(path_, error);
    }
  }

  int fd_ = -1;
  fs::path path_;
};

[[nodiscard]] NativeUnixSocket bind_native_socket(const fs::path& socket_path,
                                                  const bool listen_active) {
  const int socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    throw std::runtime_error("failed to create native unix socket");
  }

  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  const std::string raw_path = socket_path.string();
  if (raw_path.size() >= sizeof(address.sun_path)) {
    ::close(socket_fd);
    throw std::runtime_error("socket path exceeds sockaddr_un capacity");
  }

  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", raw_path.c_str());
  if (::bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    const int bind_errno = errno;
    ::close(socket_fd);
    throw std::runtime_error("failed to bind native unix socket: errno=" +
                             std::to_string(bind_errno));
  }

  if (::chmod(raw_path.c_str(), 0600) != 0) {
    const int chmod_errno = errno;
    ::close(socket_fd);
    throw std::runtime_error("failed to chmod native unix socket: errno=" +
                             std::to_string(chmod_errno));
  }

  if (listen_active && ::listen(socket_fd, 4) != 0) {
    const int listen_errno = errno;
    ::close(socket_fd);
    throw std::runtime_error("failed to listen on native unix socket: errno=" +
                             std::to_string(listen_errno));
  }

  return NativeUnixSocket(socket_fd, socket_path);
}

class RecordingIpc final : public dasall::platform::IIPC {
 public:
  int listen_calls = 0;
  std::optional<IpcEndpoint> last_endpoint;
  std::optional<ListenOptions> last_options;
  PlatformResult<IpcListenerHandle> listen_result =
      PlatformResult<IpcListenerHandle>::success(IpcListenerHandle{.native_fd = 99U});

  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint& endpoint,
                                           const ListenOptions& options) override {
    ++listen_calls;
    last_endpoint = endpoint;
    last_options = options;
    return listen_result;
  }

  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "accept is not used by DaemonListenerHostBindConflictTest"));
  }

  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "connect is not used by DaemonListenerHostBindConflictTest"));
  }

  PlatformResult<IpcSendResult> send(const IpcChannelHandle&, const IpcPayload&) override {
    return PlatformResult<IpcSendResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "send is not used by DaemonListenerHostBindConflictTest"));
  }

  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle&, std::int32_t) override {
    return PlatformResult<IpcReceiveResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "receive is not used by DaemonListenerHostBindConflictTest"));
  }

  PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle&) override {
    return PlatformResult<PeerIdentitySnapshot>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "describe_peer is not used by DaemonListenerHostBindConflictTest"));
  }

  PlatformResult<bool> close(const IpcChannelHandle&) override {
    return PlatformResult<bool>::success(true);
  }
};

void test_bind_rejects_active_socket_without_calling_listen() {
  ScopedTempDirectory temp_root("daemon-listener-bind-active");
  const fs::path socket_path = temp_root.path() / "active.sock";
  auto active_socket = bind_native_socket(socket_path, true);

  auto ipc = std::make_shared<RecordingIpc>();
  DaemonListenerHost host(ipc);

  IpcEndpoint endpoint;
  endpoint.socket_path = socket_path.string();

  const auto bind_result = host.bind(endpoint);
  assert_true(!bind_result.ok(), "bind should fail when an active socket already occupies the endpoint");
  assert_equal(static_cast<int>(PlatformErrorCode::AddressInUse),
               static_cast<int>(bind_result.error->code),
               "active socket conflicts should map to AddressInUse");
  assert_equal(0, ipc->listen_calls,
               "bind preflight should fail before calling ipc.listen for active sockets");
  assert_true(fs::exists(socket_path), "active socket conflicts must not delete the existing socket path");
}

void test_bind_removes_stale_socket_before_listen() {
  ScopedTempDirectory temp_root("daemon-listener-bind-stale");
  const fs::path socket_path = temp_root.path() / "stale.sock";
  auto stale_socket = bind_native_socket(socket_path, false);
  stale_socket.close_keep_path();

  auto ipc = std::make_shared<RecordingIpc>();
  DaemonListenerHost host(ipc);

  IpcEndpoint endpoint;
  endpoint.socket_path = socket_path.string();

  const auto bind_result = host.bind(endpoint);
  assert_true(bind_result.ok(), "bind should accept owned stale sockets after cleanup");
  assert_equal(1, ipc->listen_calls,
               "bind should call ipc.listen once after stale socket cleanup succeeds");
  assert_true(!fs::exists(socket_path),
              "stale socket path should be removed during bind preflight before listener creation");
}

}  // namespace

int main() {
  try {
    test_bind_rejects_active_socket_without_calling_listen();
    test_bind_removes_stale_socket_before_listen();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonListenerHostBindConflictTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}