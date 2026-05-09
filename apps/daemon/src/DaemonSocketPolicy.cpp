#include "DaemonSocketPolicy.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "PlatformError.h"

namespace dasall::apps::daemon {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] dasall::platform::PlatformError make_error(
    dasall::platform::PlatformErrorCode code,
    dasall::platform::PlatformErrorCategory category,
    std::string detail,
    std::string syscall_name = {},
    std::optional<int> errno_value = std::nullopt) {
  return dasall::platform::PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = std::move(syscall_name),
      .errno_value = errno_value,
      .detail = std::move(detail),
  };
}

[[nodiscard]] std::uint32_t mode_bits(const struct stat& entry_stat) {
  return static_cast<std::uint32_t>(entry_stat.st_mode & 0777);
}

[[nodiscard]] bool contains_relative_component(const fs::path& path) {
  for (const auto& component : path) {
    if (component == "." || component == "..") {
      return true;
    }
  }

  return false;
}

[[nodiscard]] dasall::platform::PlatformErrorCode map_errno_to_code(
    const int error_number,
    const dasall::platform::PlatformErrorCode fallback) {
  switch (error_number) {
    case EACCES:
    case EPERM:
      return dasall::platform::PlatformErrorCode::PermissionDenied;
    case EADDRINUSE:
      return dasall::platform::PlatformErrorCode::AddressInUse;
    case ENOENT:
      return dasall::platform::PlatformErrorCode::NotFound;
    default:
      return fallback;
  }
}

[[nodiscard]] dasall::platform::PlatformResult<bool> validate_parent_directory(
    const fs::path& parent_path,
    const DaemonSocketPolicy& policy,
    const bool allow_missing_parent) {
  struct stat parent_stat {};
  if (::stat(parent_path.c_str(), &parent_stat) != 0) {
    if (errno == ENOENT && allow_missing_parent) {
      return dasall::platform::PlatformResult<bool>::success(true);
    }

    return dasall::platform::PlatformResult<bool>::failure(make_error(
        map_errno_to_code(errno, dasall::platform::PlatformErrorCode::InternalFailure),
        errno == ENOENT ? dasall::platform::PlatformErrorCategory::Validation
                        : dasall::platform::PlatformErrorCategory::IO,
        "socket parent directory is not accessible: " + parent_path.string(),
        "stat",
        errno));
  }

  if (!S_ISDIR(parent_stat.st_mode)) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "socket parent path must resolve to a directory: " + parent_path.string()));
  }

  if (static_cast<std::uint32_t>(parent_stat.st_uid) != policy.expected_owner_uid ||
      static_cast<std::uint32_t>(parent_stat.st_gid) != policy.expected_owner_gid) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::PermissionDenied,
        dasall::platform::PlatformErrorCategory::Validation,
        "socket parent directory owner/group does not match daemon socket policy: " +
            parent_path.string()));
  }

  if ((mode_bits(parent_stat) & 0002U) != 0U) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::PermissionDenied,
        dasall::platform::PlatformErrorCategory::Validation,
        "socket parent directory cannot be world-writable: " + parent_path.string()));
  }

  if ((mode_bits(parent_stat) & policy.required_parent_mode) !=
      policy.required_parent_mode) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::PermissionDenied,
        dasall::platform::PlatformErrorCategory::Validation,
        "socket parent directory is missing required owner permissions: " +
            parent_path.string()));
  }

  return dasall::platform::PlatformResult<bool>::success(true);
}

[[nodiscard]] dasall::platform::PlatformResult<bool> probe_existing_socket(
    const fs::path& socket_path) {
  const int probe_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe_fd < 0) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        map_errno_to_code(errno, dasall::platform::PlatformErrorCode::InternalFailure),
        dasall::platform::PlatformErrorCategory::IO,
        "failed to create unix socket liveness probe for " + socket_path.string(),
        "socket",
        errno));
  }

  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  const std::string raw_path = socket_path.string();
  if (raw_path.size() >= sizeof(address.sun_path)) {
    (void)::close(probe_fd);
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "socket path exceeds sockaddr_un capacity: " + raw_path));
  }

  std::strncpy(address.sun_path, raw_path.c_str(), sizeof(address.sun_path) - 1U);
  if (::connect(probe_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
    (void)::close(probe_fd);
    return dasall::platform::PlatformResult<bool>::success(true);
  }

  const int connect_errno = errno;
  (void)::close(probe_fd);
  switch (connect_errno) {
    case ECONNREFUSED:
    case ENOENT:
      return dasall::platform::PlatformResult<bool>::success(false);
    case EACCES:
    case EPERM:
      return dasall::platform::PlatformResult<bool>::failure(make_error(
          dasall::platform::PlatformErrorCode::PermissionDenied,
          dasall::platform::PlatformErrorCategory::IO,
          "existing socket cannot be probed due to permissions: " + raw_path,
          "connect",
          connect_errno));
    default:
      return dasall::platform::PlatformResult<bool>::failure(make_error(
          map_errno_to_code(connect_errno,
                            dasall::platform::PlatformErrorCode::InternalFailure),
          dasall::platform::PlatformErrorCategory::IO,
          "existing socket liveness probe failed for " + raw_path,
          "connect",
          connect_errno));
  }
}

}  // namespace

DaemonSocketPolicy DaemonSocketPolicy::for_current_process() {
  return DaemonSocketPolicy{
      .expected_owner_uid = static_cast<std::uint32_t>(::getuid()),
      .expected_owner_gid = static_cast<std::uint32_t>(::getgid()),
      .required_parent_mode = 0700U,
      .required_socket_mode = 0600U,
  };
}

bool DaemonSocketPolicy::has_consistent_values() const {
  return required_parent_mode != 0U && required_socket_mode != 0U;
}

dasall::platform::PlatformResult<bool> validate_socket_path(
    const std::string& socket_path,
    const DaemonSocketPolicy& policy) {
  if (!policy.has_consistent_values()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "daemon socket policy is inconsistent"));
  }

  if (socket_path.empty()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "daemon.socket_path must not be empty"));
  }

  const fs::path parsed_path(socket_path);
  if (!parsed_path.is_absolute()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "daemon.socket_path must be an absolute path"));
  }

  if (contains_relative_component(parsed_path)) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "daemon.socket_path must not contain '.' or '..' path traversal components"));
  }

  if (parsed_path.filename().empty()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "daemon.socket_path must include a socket filename"));
  }

  sockaddr_un address {};
  const std::string raw_path = parsed_path.string();
  if (raw_path.size() >= sizeof(address.sun_path)) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "daemon.socket_path exceeds sockaddr_un capacity: " + raw_path));
  }

  return validate_parent_directory(parsed_path.parent_path(), policy, true);
}

dasall::platform::PlatformResult<bool> try_cleanup_stale_socket(
    const fs::path& socket_path,
    const DaemonSocketPolicy& policy) {
  struct stat socket_stat {};
  if (::lstat(socket_path.c_str(), &socket_stat) != 0) {
    if (errno == ENOENT) {
      return dasall::platform::PlatformResult<bool>::success(false);
    }

    return dasall::platform::PlatformResult<bool>::failure(make_error(
        map_errno_to_code(errno, dasall::platform::PlatformErrorCode::InternalFailure),
        dasall::platform::PlatformErrorCategory::IO,
        "failed to inspect existing socket path: " + socket_path.string(),
        "lstat",
        errno));
  }

  if (!S_ISSOCK(socket_stat.st_mode)) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::AddressInUse,
        dasall::platform::PlatformErrorCategory::Validation,
        "existing bind path is not a unix socket and cannot be cleaned safely: " +
            socket_path.string()));
  }

  if (static_cast<std::uint32_t>(socket_stat.st_uid) != policy.expected_owner_uid ||
      static_cast<std::uint32_t>(socket_stat.st_gid) != policy.expected_owner_gid) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::PermissionDenied,
        dasall::platform::PlatformErrorCategory::Validation,
        "existing socket owner/group does not match daemon socket policy: " +
            socket_path.string()));
  }

  if (mode_bits(socket_stat) != policy.required_socket_mode) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::PermissionDenied,
        dasall::platform::PlatformErrorCategory::Validation,
        "existing socket mode does not match daemon cleanup policy: " +
            socket_path.string()));
  }

  const auto probe = probe_existing_socket(socket_path);
  if (!probe.ok()) {
    return probe;
  }

  if (probe.value.value_or(false)) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::AddressInUse,
        dasall::platform::PlatformErrorCategory::IPC,
        "active unix socket cannot be removed during bind preflight: " +
            socket_path.string()));
  }

  if (::unlink(socket_path.c_str()) != 0) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        map_errno_to_code(errno, dasall::platform::PlatformErrorCode::InternalFailure),
        dasall::platform::PlatformErrorCategory::IO,
        "failed to remove stale unix socket before bind: " + socket_path.string(),
        "unlink",
        errno));
  }

  return dasall::platform::PlatformResult<bool>::success(true);
}

dasall::platform::PlatformResult<bool> preflight_bind_endpoint(
    const dasall::platform::IpcEndpoint& endpoint,
    const DaemonSocketPolicy& policy) {
  if (!endpoint.has_consistent_values()) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "listener endpoint is invalid"));
  }

  const auto path_validation = validate_socket_path(endpoint.socket_path, policy);
  if (!path_validation.ok()) {
    return path_validation;
  }

  const fs::path socket_path(endpoint.socket_path);
  const fs::path parent_path = socket_path.parent_path();
  std::error_code filesystem_error;
  if (!fs::exists(parent_path, filesystem_error)) {
    filesystem_error.clear();
    if (!fs::create_directories(parent_path, filesystem_error) && filesystem_error) {
      return dasall::platform::PlatformResult<bool>::failure(make_error(
          filesystem_error == std::errc::permission_denied
              ? dasall::platform::PlatformErrorCode::PermissionDenied
              : dasall::platform::PlatformErrorCode::InternalFailure,
          dasall::platform::PlatformErrorCategory::IO,
          "failed to create socket parent directory: " + parent_path.string()));
    }

    fs::permissions(parent_path,
                    static_cast<fs::perms>(policy.required_parent_mode),
                    fs::perm_options::replace,
                    filesystem_error);
    if (filesystem_error) {
      return dasall::platform::PlatformResult<bool>::failure(make_error(
          filesystem_error == std::errc::permission_denied
              ? dasall::platform::PlatformErrorCode::PermissionDenied
              : dasall::platform::PlatformErrorCode::InternalFailure,
          dasall::platform::PlatformErrorCategory::IO,
          "failed to apply socket parent directory permissions: " +
              parent_path.string()));
    }
  }

  const auto parent_validation = validate_parent_directory(parent_path, policy, false);
  if (!parent_validation.ok()) {
    return parent_validation;
  }

  filesystem_error.clear();
  if (!fs::exists(socket_path, filesystem_error)) {
    return dasall::platform::PlatformResult<bool>::success(true);
  }

  if (filesystem_error) {
    return dasall::platform::PlatformResult<bool>::failure(make_error(
        filesystem_error == std::errc::permission_denied
            ? dasall::platform::PlatformErrorCode::PermissionDenied
            : dasall::platform::PlatformErrorCode::InternalFailure,
        dasall::platform::PlatformErrorCategory::IO,
        "failed to inspect existing socket path before bind: " + socket_path.string()));
  }

  return try_cleanup_stale_socket(socket_path, policy);
}

}  // namespace dasall::apps::daemon