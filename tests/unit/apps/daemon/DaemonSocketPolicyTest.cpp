#include <cerrno>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "DaemonSocketPolicy.h"
#include "PlatformError.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::apps::daemon::DaemonSocketPolicy;
using dasall::apps::daemon::preflight_bind_endpoint;
using dasall::apps::daemon::try_cleanup_stale_socket;
using dasall::apps::daemon::validate_socket_path;
using dasall::platform::IpcEndpoint;
using dasall::platform::PlatformErrorCode;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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
                                                  const mode_t socket_mode,
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

  if (::chmod(raw_path.c_str(), socket_mode) != 0) {
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

void test_validate_socket_path_accepts_secure_parent_directory() {
  ScopedTempDirectory temp_root("daemon-socket-policy-accept");
  const fs::path socket_parent = temp_root.path() / "control";
  fs::create_directories(socket_parent);
  fs::permissions(socket_parent, static_cast<fs::perms>(0700), fs::perm_options::replace);

  const auto result = validate_socket_path(
      (socket_parent / "daemon.sock").string(),
      DaemonSocketPolicy::for_current_process());
  assert_true(result.ok(), "validate_socket_path should accept secure absolute daemon socket paths");
}

void test_validate_socket_path_rejects_traversal_component() {
  const auto result = validate_socket_path(
      "/tmp/dasall/../daemon.sock",
      DaemonSocketPolicy::for_current_process());
  assert_true(!result.ok(), "validate_socket_path should reject traversal components");
  assert_equal(static_cast<int>(PlatformErrorCode::InvalidArgument),
               static_cast<int>(result.error->code),
               "traversal paths should map to InvalidArgument");
}

void test_try_cleanup_stale_socket_removes_owned_socket() {
  ScopedTempDirectory temp_root("daemon-socket-policy-stale");
  const fs::path socket_path = temp_root.path() / "stale.sock";
  auto socket_handle = bind_native_socket(socket_path, 0600, false);
  socket_handle.close_keep_path();

  const auto result = try_cleanup_stale_socket(
      socket_path,
      DaemonSocketPolicy::for_current_process());
  assert_true(result.ok(), "try_cleanup_stale_socket should accept owned stale sockets");
  assert_true(result.value.value_or(false), "stale socket cleanup should report that it removed the socket");
  assert_true(!fs::exists(socket_path), "stale socket cleanup should unlink the stale socket path");
}

void test_try_cleanup_stale_socket_rejects_mode_mismatch() {
  ScopedTempDirectory temp_root("daemon-socket-policy-mode");
  const fs::path socket_path = temp_root.path() / "stale-mode.sock";
  auto socket_handle = bind_native_socket(socket_path, 0666, false);
  socket_handle.close_keep_path();

  const auto result = try_cleanup_stale_socket(
      socket_path,
      DaemonSocketPolicy::for_current_process());
  assert_true(!result.ok(), "try_cleanup_stale_socket should reject stale sockets whose mode does not match policy");
  assert_equal(static_cast<int>(PlatformErrorCode::PermissionDenied),
               static_cast<int>(result.error->code),
               "mode mismatch should map to PermissionDenied");
  assert_true(fs::exists(socket_path), "mode-mismatched stale sockets must not be removed");
}

void test_preflight_bind_endpoint_creates_missing_parent_directory() {
  ScopedTempDirectory temp_root("daemon-socket-policy-preflight");
  const fs::path socket_path = temp_root.path() / "nested" / "daemon.sock";

  IpcEndpoint endpoint;
  endpoint.socket_path = socket_path.string();

  const auto result = preflight_bind_endpoint(
      endpoint,
      DaemonSocketPolicy::for_current_process());
  assert_true(result.ok(), "preflight_bind_endpoint should create missing daemon socket parents");
  assert_true(fs::exists(socket_path.parent_path()), "preflight_bind_endpoint should materialize the parent directory");
}

}  // namespace

int main() {
  try {
    test_validate_socket_path_accepts_secure_parent_directory();
    test_validate_socket_path_rejects_traversal_component();
    test_try_cleanup_stale_socket_removes_owned_socket();
    test_try_cleanup_stale_socket_rejects_mode_mismatch();
    test_preflight_bind_endpoint_creates_missing_parent_directory();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonSocketPolicyTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}