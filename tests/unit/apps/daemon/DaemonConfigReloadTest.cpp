#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "DaemonConfig.h"
#include "DaemonConfigReloader.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonBootstrapConfig;
using dasall::apps::daemon::DaemonConfigReloader;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_key(const std::vector<std::string>& keys,
                                const std::string& key) {
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

void test_reload_apply_accepts_diag_enabled_only() {
  DaemonBootstrapConfig base;
  assert_true(!base.override_enabled,
              "override enable should stay disabled by default");

  DaemonConfigReloader reloader(base);

  DaemonBootstrapConfig candidate = base;
  candidate.diag_enabled = true;

  const auto result = reloader.apply_reload_snapshot(candidate);
  assert_true(result.ok(),
              "reload should accept diag_enabled as the only runtime-mutable daemon config key");
  assert_true(result.rejected_keys.empty(),
              "diag-only reload should not report rejected keys");
  assert_equal(1, static_cast<int>(result.changed_keys.size()),
               "diag-only reload should expose exactly one changed key");
  assert_equal(std::string("daemon.diag_enabled"), result.changed_keys.front(),
               "diag-only reload should surface daemon.diag_enabled as the changed key");

  const auto& active = reloader.active_snapshot();
  assert_true(active.diag_enabled,
              "diag-only reload should update the diagnostics gate state");
}

void test_reload_rejects_bootstrap_only_keys_and_preserves_snapshot() {
  DaemonBootstrapConfig base;
  DaemonConfigReloader reloader(base);

  DaemonBootstrapConfig candidate = base;
  candidate.log_level = "debug";
  candidate.log_format = "text";
  candidate.receipt_ttl_sec = 1800;
  candidate.override_enabled = true;
  candidate.watchdog_enabled = true;

  const auto result = reloader.apply_reload_snapshot(candidate);
  assert_true(!result.ok(),
              "reload should reject daemon bootstrap keys that are no longer runtime-mutable");
  assert_equal(std::string("reload_rejected_restart_only_keys"), result.reason,
               "bootstrap-only rejection should preserve the stable rejection reason");
  assert_true(contains_key(result.rejected_keys, "daemon.log_level"),
              "reload rejection should include daemon.log_level");
  assert_true(contains_key(result.rejected_keys, "daemon.log_format"),
              "reload rejection should include daemon.log_format");
  assert_true(contains_key(result.rejected_keys, "daemon.receipt_ttl_sec"),
              "reload rejection should include daemon.receipt_ttl_sec");
  assert_true(contains_key(result.rejected_keys, "daemon.override_enabled"),
              "reload rejection should include daemon.override_enabled");
  assert_true(contains_key(result.rejected_keys, "daemon.watchdog_enabled"),
              "reload rejection should include daemon.watchdog_enabled");

  const auto& active = reloader.active_snapshot();
  assert_equal(base.receipt_ttl_sec, active.receipt_ttl_sec,
               "rejected bootstrap-only reload must preserve the active receipt ttl");
  assert_equal(base.log_format, active.log_format,
               "rejected bootstrap-only reload must preserve the active log format");
}

void test_reload_rejects_restart_only_keys_and_preserves_snapshot() {
  DaemonBootstrapConfig base;
  std::vector<std::string> audited_keys;
  std::string audited_reason;

  DaemonConfigReloader reloader(
      base,
      [&audited_keys, &audited_reason](const std::vector<std::string>& keys,
                                       const std::string& reason) {
        audited_keys = keys;
        audited_reason = reason;
      });

  DaemonBootstrapConfig candidate = base;
  candidate.socket_path = "/tmp/dasall/reload-denied.sock";

  const auto result = reloader.apply_reload_snapshot(candidate);
  assert_true(!result.ok(),
              "reload should reject restart-only socket_path update");
  assert_equal(static_cast<std::size_t>(1), result.rejected_keys.size(),
               "reload rejection should contain one rejected key");
  assert_equal(std::string("daemon.socket_path"), result.rejected_keys.front(),
               "reload rejection should surface socket_path as rejected key");

  const auto& active = reloader.active_snapshot();
  assert_equal(base.socket_path, active.socket_path,
               "rejected reload must preserve active socket_path snapshot");

  const auto& last_good = reloader.last_known_good_snapshot();
  assert_equal(base.socket_path, last_good.socket_path,
               "rejected reload must preserve last-known-good snapshot");

  assert_equal(static_cast<std::size_t>(1), audited_keys.size(),
               "reload denied path should emit one audited key");
  assert_equal(std::string("daemon.socket_path"), audited_keys.front(),
               "reload denied audit should include rejected key");
  assert_equal(std::string("reload_rejected_restart_only_keys"), audited_reason,
               "reload denied audit should preserve rejection reason");
}

}  // namespace

int main() {
  try {
    test_reload_apply_accepts_diag_enabled_only();
    test_reload_rejects_bootstrap_only_keys_and_preserves_snapshot();
    test_reload_rejects_restart_only_keys_and_preserves_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonConfigReloadTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
