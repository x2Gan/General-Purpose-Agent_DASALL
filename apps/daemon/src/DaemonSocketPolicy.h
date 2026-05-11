#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "IIPC.h"
#include "PlatformResult.h"

namespace dasall::apps::daemon {

struct DaemonSocketPolicy {
  std::uint32_t expected_owner_uid = 0U;
  std::uint32_t expected_owner_gid = 0U;
  std::uint32_t required_parent_mode = 0700U;
  std::uint32_t required_socket_mode = 0600U;

  [[nodiscard]] static DaemonSocketPolicy for_current_process();
    [[nodiscard]] static DaemonSocketPolicy for_daemon_service_account_or_current_process();

  [[nodiscard]] bool has_consistent_values() const;
};

[[nodiscard]] dasall::platform::PlatformResult<bool> validate_socket_path(
    const std::string& socket_path,
    const DaemonSocketPolicy& policy = DaemonSocketPolicy::for_current_process());

[[nodiscard]] dasall::platform::PlatformResult<bool> preflight_bind_endpoint(
    const dasall::platform::IpcEndpoint& endpoint,
    const DaemonSocketPolicy& policy = DaemonSocketPolicy::for_current_process());

[[nodiscard]] dasall::platform::PlatformResult<bool> try_cleanup_stale_socket(
    const std::filesystem::path& socket_path,
    const DaemonSocketPolicy& policy = DaemonSocketPolicy::for_current_process());

}  // namespace dasall::apps::daemon