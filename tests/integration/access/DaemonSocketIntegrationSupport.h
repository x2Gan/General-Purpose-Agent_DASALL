#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "support/TestAssertions.h"

namespace dasall::tests::integration::access_support {

namespace fs = std::filesystem;

class SocketScopedTempDirectory {
 public:
  explicit SocketScopedTempDirectory(std::string_view stem)
      : path_(fs::temp_directory_path() /
              (std::string(stem) + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    fs::create_directories(path_);
    fs::permissions(path_, static_cast<fs::perms>(0700), fs::perm_options::replace);
  }

  ~SocketScopedTempDirectory() {
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

[[nodiscard]] inline NativeUnixSocket bind_native_socket(
    const fs::path& socket_path,
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

[[nodiscard]] inline dasall::access::DaemonAccessPipelineOptions make_ping_only_options(
    const std::string& profile_id) {
  dasall::access::DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {
      "local://uid/" + std::to_string(static_cast<unsigned int>(::getuid()))};
  options.daemon_profile_id = profile_id;
  options.daemon_version = "v1";
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };
  return options;
}

[[nodiscard]] inline std::uint32_t socket_mode_bits(const fs::path& socket_path) {
  struct stat socket_stat {};
  const auto stat_result = ::lstat(socket_path.c_str(), &socket_stat);
  dasall::tests::support::assert_true(
      stat_result == 0,
      "daemon socket integration should materialize a filesystem socket path");
  dasall::tests::support::assert_true(
      S_ISSOCK(socket_stat.st_mode),
      "daemon socket integration should create a unix socket entry on disk");
  return static_cast<std::uint32_t>(socket_stat.st_mode & 0777);
}

}  // namespace dasall::tests::integration::access_support