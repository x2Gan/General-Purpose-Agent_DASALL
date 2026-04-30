#pragma once

#include <functional>
#include <string>
#include <vector>

#include "DaemonConfig.h"

namespace dasall::apps::daemon {

struct DaemonReloadResult {
  bool applied = false;
  std::vector<std::string> changed_keys;
  std::vector<std::string> rejected_keys;
  std::string reason;

  [[nodiscard]] bool ok() const {
    return applied;
  }
};

class DaemonConfigReloader {
 public:
  using ReloadDeniedAudit =
      std::function<void(const std::vector<std::string>& rejected_keys,
                         const std::string& reason)>;

  explicit DaemonConfigReloader(
      DaemonBootstrapConfig initial_snapshot,
      ReloadDeniedAudit denied_audit = {});

  [[nodiscard]] DaemonReloadResult reload_allowed_keys(
      const DaemonBootstrapConfig& candidate) const;

  [[nodiscard]] DaemonReloadResult apply_reload_snapshot(
      const DaemonBootstrapConfig& candidate);

  [[nodiscard]] const DaemonBootstrapConfig& active_snapshot() const;
  [[nodiscard]] const DaemonBootstrapConfig& last_known_good_snapshot() const;

 private:
  [[nodiscard]] static std::vector<std::string> collect_changed_keys(
      const DaemonBootstrapConfig& from,
      const DaemonBootstrapConfig& to);

  [[nodiscard]] static bool is_allowlisted_reload_key(const std::string& key);

  DaemonBootstrapConfig active_snapshot_;
  DaemonBootstrapConfig last_known_good_snapshot_;
  ReloadDeniedAudit denied_audit_;
};

}  // namespace dasall::apps::daemon
