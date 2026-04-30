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

void test_reload_apply_accepts_allowlisted_keys() {
  DaemonBootstrapConfig base;
  assert_true(!base.override_enabled,
              "override enable should stay disabled by default");

  DaemonConfigReloader reloader(base);

  DaemonBootstrapConfig candidate = base;
  candidate.log_level = "debug";
  candidate.log_format = "text";
  candidate.diag_enabled = true;
  candidate.watchdog_enabled = true;
  candidate.receipt_ttl_sec = 1800;
  candidate.override_enabled = true;

  const auto result = reloader.apply_reload_snapshot(candidate);
  assert_true(result.ok(),
              "reload should accept allowlisted daemon config keys");
  assert_true(result.rejected_keys.empty(),
              "allowlisted reload should not report rejected keys");

  const auto& active = reloader.active_snapshot();
  assert_equal(std::string("debug"), active.log_level,
               "allowlisted reload should update log level");
  assert_equal(std::string("text"), active.log_format,
               "allowlisted reload should update log format");
  assert_true(active.diag_enabled,
              "allowlisted reload should update diag enable");
  assert_true(active.watchdog_enabled,
              "allowlisted reload should update watchdog enable");
  assert_equal(1800, active.receipt_ttl_sec,
               "allowlisted reload should update receipt ttl");
  assert_true(active.override_enabled,
              "allowlisted reload should update override enable");
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

void test_reload_rejects_invalid_snapshot_without_replacing_last_good() {
  DaemonBootstrapConfig base;
  DaemonConfigReloader reloader(base);

  DaemonBootstrapConfig candidate = base;
  candidate.receipt_ttl_sec = 0;

  const auto result = reloader.apply_reload_snapshot(candidate);
  assert_true(!result.ok(),
              "reload should reject inconsistent candidate snapshot");
  assert_equal(std::string("reload_rejected_invalid_snapshot"), result.reason,
               "invalid snapshot rejection should expose stable reason");

  const auto& active = reloader.active_snapshot();
  assert_equal(base.receipt_ttl_sec, active.receipt_ttl_sec,
               "invalid reload must preserve active receipt ttl");

  const auto& last_good = reloader.last_known_good_snapshot();
  assert_equal(base.receipt_ttl_sec, last_good.receipt_ttl_sec,
               "invalid reload must preserve last-known-good receipt ttl");
}

}  // namespace

int main() {
  try {
    test_reload_apply_accepts_allowlisted_keys();
    test_reload_rejects_restart_only_keys_and_preserves_snapshot();
    test_reload_rejects_invalid_snapshot_without_replacing_last_good();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonConfigReloadTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
