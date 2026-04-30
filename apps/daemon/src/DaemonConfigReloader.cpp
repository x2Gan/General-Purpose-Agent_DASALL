#include "DaemonConfigReloader.h"

#include <utility>

namespace dasall::apps::daemon {

DaemonConfigReloader::DaemonConfigReloader(
    DaemonBootstrapConfig initial_snapshot,
    ReloadDeniedAudit denied_audit)
    : active_snapshot_(std::move(initial_snapshot)),
      last_known_good_snapshot_(active_snapshot_),
      denied_audit_(std::move(denied_audit)) {}

DaemonReloadResult DaemonConfigReloader::reload_allowed_keys(
    const DaemonBootstrapConfig& candidate) const {
  DaemonReloadResult result;
  result.changed_keys = collect_changed_keys(active_snapshot_, candidate);

  for (const auto& key : result.changed_keys) {
    if (!is_allowlisted_reload_key(key)) {
      result.rejected_keys.push_back(key);
    }
  }

  if (!result.rejected_keys.empty()) {
    result.reason = "reload_rejected_restart_only_keys";
    return result;
  }

  if (!candidate.has_consistent_values()) {
    result.reason = "reload_rejected_invalid_snapshot";
    return result;
  }

  result.applied = true;
  result.reason = "reload_applied";
  return result;
}

DaemonReloadResult DaemonConfigReloader::apply_reload_snapshot(
    const DaemonBootstrapConfig& candidate) {
  auto result = reload_allowed_keys(candidate);
  if (!result.ok()) {
    if (!result.rejected_keys.empty() && denied_audit_) {
      denied_audit_(result.rejected_keys, result.reason);
    }
    return result;
  }

  active_snapshot_ = candidate;
  last_known_good_snapshot_ = active_snapshot_;
  return result;
}

const DaemonBootstrapConfig& DaemonConfigReloader::active_snapshot() const {
  return active_snapshot_;
}

const DaemonBootstrapConfig& DaemonConfigReloader::last_known_good_snapshot()
    const {
  return last_known_good_snapshot_;
}

std::vector<std::string> DaemonConfigReloader::collect_changed_keys(
    const DaemonBootstrapConfig& from,
    const DaemonBootstrapConfig& to) {
  std::vector<std::string> keys;

  if (from.socket_path != to.socket_path) {
    keys.push_back("daemon.socket_path");
  }
  if (from.listen_backlog != to.listen_backlog) {
    keys.push_back("daemon.listen_backlog");
  }
  if (from.max_payload_bytes != to.max_payload_bytes) {
    keys.push_back("daemon.max_payload_bytes");
  }
  if (from.dispatch_timeout_ms != to.dispatch_timeout_ms) {
    keys.push_back("daemon.dispatch_timeout_ms");
  }
  if (from.shutdown_grace_ms != to.shutdown_grace_ms) {
    keys.push_back("daemon.shutdown_grace_ms");
  }
  if (from.receipt_ttl_sec != to.receipt_ttl_sec) {
    keys.push_back("daemon.receipt_ttl_sec");
  }
  if (from.accept_workers != to.accept_workers) {
    keys.push_back("daemon.accept_workers");
  }
  if (from.dispatch_workers != to.dispatch_workers) {
    keys.push_back("daemon.dispatch_workers");
  }
  if (from.diag_enabled != to.diag_enabled) {
    keys.push_back("daemon.diag_enabled");
  }
  if (from.override_enabled != to.override_enabled) {
    keys.push_back("daemon.override_enabled");
  }
  if (from.watchdog_enabled != to.watchdog_enabled) {
    keys.push_back("daemon.watchdog_enabled");
  }
  if (from.log_level != to.log_level) {
    keys.push_back("daemon.log_level");
  }
  if (from.log_format != to.log_format) {
    keys.push_back("daemon.log_format");
  }
  if (from.startup_mode != to.startup_mode) {
    keys.push_back("daemon.startup_mode");
  }

  return keys;
}

bool DaemonConfigReloader::is_allowlisted_reload_key(const std::string& key) {
  return key == "daemon.log_level" || key == "daemon.log_format" ||
         key == "daemon.diag_enabled" || key == "daemon.watchdog_enabled" ||
         key == "daemon.receipt_ttl_sec" ||
         key == "daemon.override_enabled";
}

}  // namespace dasall::apps::daemon
