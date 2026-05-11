#pragma once

#include <optional>
#include <string>
#include <vector>

#include "DaemonConfig.h"
#include "DaemonSocketPolicy.h"

namespace dasall::apps::daemon {

enum class DaemonConfigValidationError {
  Conflict = 0,
  InvalidSocketPath = 1,
  InvalidPayloadLimit = 2,
  ReloadForbidden = 3,
};

struct DaemonConfigValidationResult {
  bool accepted = false;
  std::optional<DaemonConfigValidationError> error_code;
  std::vector<std::string> key_paths;
  std::string message;

  [[nodiscard]] bool ok() const {
    return accepted && !error_code.has_value();
  }

  [[nodiscard]] static DaemonConfigValidationResult success(
      std::string message = "config validation passed");

  [[nodiscard]] static DaemonConfigValidationResult failure(
      DaemonConfigValidationError error_code,
      std::vector<std::string> key_paths,
      std::string message);
};

class DaemonConfigValidator {
 public:
  [[nodiscard]] DaemonConfigValidationResult validate_config(
    const DaemonBootstrapConfig& config,
    const DaemonSocketPolicy& socket_policy =
      DaemonSocketPolicy::for_current_process()) const;

  [[nodiscard]] DaemonConfigValidationResult validate_conflicts(
      const std::vector<DaemonConfigConflict>& conflicts) const;

  [[nodiscard]] DaemonConfigValidationResult validate_reload_keys(
      const std::vector<std::string>& candidate_keys) const;

  [[nodiscard]] DaemonConfigValidationResult validate_only(
      const DaemonBootstrapConfig& config,
      const std::vector<DaemonConfigConflict>& conflicts = {},
      const std::vector<std::string>& reload_keys = {},
      const DaemonSocketPolicy& socket_policy =
        DaemonSocketPolicy::for_current_process()) const;
};

}  // namespace dasall::apps::daemon