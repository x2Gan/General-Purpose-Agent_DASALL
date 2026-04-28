#include "DaemonProfileProjection.h"

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "ProfileYamlParser.h"

namespace dasall::profiles {
namespace {

template <typename T>
[[nodiscard]] std::optional<T> get_numeric(
    const std::unordered_map<std::string, std::string>& scalars,
    const std::string& key) {
  const auto it = scalars.find(key);
  if (it == scalars.end()) {
    return std::nullopt;
  }

  try {
    if constexpr (std::is_same_v<T, std::uint32_t>) {
      return static_cast<std::uint32_t>(std::stoul(it->second));
    }

    if constexpr (std::is_same_v<T, std::int32_t>) {
      return static_cast<std::int32_t>(std::stol(it->second));
    }
  } catch (...) {
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<bool> get_bool(
    const std::unordered_map<std::string, std::string>& scalars,
    const std::string& key) {
  const auto it = scalars.find(key);
  if (it == scalars.end()) {
    return std::nullopt;
  }

  if (it->second == "true") {
    return true;
  }

  if (it->second == "false") {
    return false;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> get_string(
    const std::unordered_map<std::string, std::string>& scalars,
    const std::string& key) {
  const auto it = scalars.find(key);
  if (it == scalars.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

void mark_defaulted(DaemonProfileSettings& settings, std::string_view key) {
  settings.defaulted_keys.emplace_back(key);
}

}  // namespace

DaemonProfileProjection::DaemonProfileProjection(const IProfileCatalog& catalog)
    : catalog_(catalog) {}

DaemonProfileProjectionResult DaemonProfileProjection::load(
    const DaemonProfileProjectionRequest& request) const {
  if (!request.has_consistent_values()) {
    return DaemonProfileProjectionResult{
        .settings = std::nullopt,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

  const auto lookup = catalog_.get_profile(request.profile_id);
  if (!lookup.ok()) {
    return DaemonProfileProjectionResult{
        .settings = std::nullopt,
        .error_code = lookup.error_code,
    };
  }

  const ParsedProfileYaml parsed_yaml =
      parse_profile_yaml_file(lookup.profile->asset_paths.runtime_policy_path);
  if (!parsed_yaml.ok) {
    return DaemonProfileProjectionResult{
        .settings = std::nullopt,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

  DaemonProfileSettings settings;
  settings.effective_profile_id = lookup.profile->profile_id;

  if (const auto socket_path =
          get_string(parsed_yaml.scalar_values, std::string(kDaemonSocketPathKey));
      socket_path.has_value()) {
    settings.socket_path = *socket_path;
  } else {
    mark_defaulted(settings, kDaemonSocketPathKey);
  }

  if (parsed_yaml.scalar_values.contains(std::string(kDaemonListenBacklogKey))) {
    const auto listen_backlog =
        get_numeric<std::uint32_t>(parsed_yaml.scalar_values,
                                   std::string(kDaemonListenBacklogKey));
    if (!listen_backlog.has_value()) {
      return DaemonProfileProjectionResult{
          .settings = std::nullopt,
          .error_code = ProfileErrorCode::SchemaInvalid,
      };
    }
    settings.listen_backlog = *listen_backlog;
  } else {
    mark_defaulted(settings, kDaemonListenBacklogKey);
  }

  if (parsed_yaml.scalar_values.contains(std::string(kDaemonDispatchTimeoutKey))) {
    const auto dispatch_timeout =
        get_numeric<std::int32_t>(parsed_yaml.scalar_values,
                                  std::string(kDaemonDispatchTimeoutKey));
    if (!dispatch_timeout.has_value()) {
      return DaemonProfileProjectionResult{
          .settings = std::nullopt,
          .error_code = ProfileErrorCode::SchemaInvalid,
      };
    }
    settings.dispatch_timeout_ms = *dispatch_timeout;
  } else {
    mark_defaulted(settings, kDaemonDispatchTimeoutKey);
  }

  if (parsed_yaml.scalar_values.contains(std::string(kDaemonDiagEnabledKey))) {
    const auto diag_enabled =
        get_bool(parsed_yaml.scalar_values, std::string(kDaemonDiagEnabledKey));
    if (!diag_enabled.has_value()) {
      return DaemonProfileProjectionResult{
          .settings = std::nullopt,
          .error_code = ProfileErrorCode::SchemaInvalid,
      };
    }
    settings.diag_enabled = *diag_enabled;
  } else {
    mark_defaulted(settings, kDaemonDiagEnabledKey);
  }

  if (parsed_yaml.scalar_values.contains(std::string(kDaemonWatchdogEnabledKey))) {
    const auto watchdog_enabled =
        get_bool(parsed_yaml.scalar_values,
                 std::string(kDaemonWatchdogEnabledKey));
    if (!watchdog_enabled.has_value()) {
      return DaemonProfileProjectionResult{
          .settings = std::nullopt,
          .error_code = ProfileErrorCode::SchemaInvalid,
      };
    }
    settings.watchdog_enabled = *watchdog_enabled;
  } else {
    mark_defaulted(settings, kDaemonWatchdogEnabledKey);
  }

  if (!settings.has_consistent_values()) {
    return DaemonProfileProjectionResult{
        .settings = std::nullopt,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

  return DaemonProfileProjectionResult{
      .settings = std::move(settings),
      .error_code = std::nullopt,
  };
}

}  // namespace dasall::profiles