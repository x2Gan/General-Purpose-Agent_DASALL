#include "DaemonConfigValidator.h"

#include <cstdint>
#include <string_view>

#include "DaemonSocketPolicy.h"

namespace dasall::apps::daemon {
namespace {

inline constexpr std::uint32_t kDaemonMaxPayloadBytesUpperBound = 4U * 1048576U;

[[nodiscard]] bool is_restart_only_reload_key(const std::string_view key) {
  return key == "daemon.socket_path" || key == "daemon.listen_backlog" ||
         key == "daemon.startup_mode" || key == "daemon.dispatch_workers" ||
         key == "daemon.accept_workers";
}

}  // namespace

DaemonConfigValidationResult DaemonConfigValidationResult::success(
    std::string message) {
  return DaemonConfigValidationResult{
      .accepted = true,
      .error_code = std::nullopt,
      .key_paths = {},
      .message = std::move(message),
  };
}

DaemonConfigValidationResult DaemonConfigValidationResult::failure(
    const DaemonConfigValidationError error_code,
    std::vector<std::string> key_paths,
    std::string message) {
  return DaemonConfigValidationResult{
      .accepted = false,
      .error_code = error_code,
      .key_paths = std::move(key_paths),
      .message = std::move(message),
  };
}

DaemonConfigValidationResult DaemonConfigValidator::validate_config(
    const DaemonBootstrapConfig& config) const {
  const auto socket_path_validation =
      validate_socket_path(config.socket_path, DaemonSocketPolicy::for_current_process());
  if (!socket_path_validation.ok()) {
    return DaemonConfigValidationResult::failure(
        DaemonConfigValidationError::InvalidSocketPath,
        {"daemon.socket_path"},
        socket_path_validation.error->detail);
  }

  if (config.max_payload_bytes == 0U ||
      config.max_payload_bytes > kDaemonMaxPayloadBytesUpperBound) {
    return DaemonConfigValidationResult::failure(
        DaemonConfigValidationError::InvalidPayloadLimit,
        {"daemon.max_payload_bytes"},
        "daemon.max_payload_bytes must stay within the supported unary control-plane limit");
  }

  if (!config.has_consistent_values()) {
    return DaemonConfigValidationResult::failure(
        DaemonConfigValidationError::InvalidSocketPath,
        {"daemon.config"},
        "daemon bootstrap config contains invalid worker or timing values");
  }

  return DaemonConfigValidationResult::success();
}

DaemonConfigValidationResult DaemonConfigValidator::validate_conflicts(
    const std::vector<DaemonConfigConflict>& conflicts) const {
  for (const auto& conflict : conflicts) {
    if (!conflict.has_consistent_values()) {
      return DaemonConfigValidationResult::failure(
          DaemonConfigValidationError::Conflict,
          {"daemon.conflicts"},
          "daemon config conflict entries must capture key, sources and both values");
    }

    return DaemonConfigValidationResult::failure(
        DaemonConfigValidationError::Conflict,
        {conflict.key},
        "daemon flags/config file conflict must be rejected instead of silently choosing one source");
  }

  return DaemonConfigValidationResult::success();
}

DaemonConfigValidationResult DaemonConfigValidator::validate_reload_keys(
    const std::vector<std::string>& candidate_keys) const {
  for (const auto& key : candidate_keys) {
    if (is_restart_only_reload_key(key)) {
      return DaemonConfigValidationResult::failure(
          DaemonConfigValidationError::ReloadForbidden,
          {key},
          "restart-only daemon config keys cannot be hot reloaded in v1");
    }
  }

  return DaemonConfigValidationResult::success();
}

DaemonConfigValidationResult DaemonConfigValidator::validate_only(
    const DaemonBootstrapConfig& config,
    const std::vector<DaemonConfigConflict>& conflicts,
    const std::vector<std::string>& reload_keys) const {
  const auto config_validation = validate_config(config);
  if (!config_validation.ok()) {
    return config_validation;
  }

  const auto conflict_validation = validate_conflicts(conflicts);
  if (!conflict_validation.ok()) {
    return conflict_validation;
  }

  const auto reload_validation = validate_reload_keys(reload_keys);
  if (!reload_validation.ok()) {
    return reload_validation;
  }

  return DaemonConfigValidationResult::success(
      "config validation passed without creating listener resources");
}

}  // namespace dasall::apps::daemon