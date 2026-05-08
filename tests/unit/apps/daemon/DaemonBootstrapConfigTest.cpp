#include <exception>
#include <iostream>
#include <string>

#include "DaemonConfig.h"
#include "support/TestAssertions.h"

namespace {

void test_bootstrap_config_defaults_match_v1_design() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::apps::daemon::DaemonStartupMode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const DaemonBootstrapConfig config;
  assert_equal(std::string("/run/dasall/daemon.sock"), config.socket_path,
               "DaemonBootstrapConfig.socket_path should match v1 default");
  assert_equal(32, static_cast<int>(config.listen_backlog),
               "DaemonBootstrapConfig.listen_backlog should match v1 default");
  assert_equal(1048576, static_cast<int>(config.max_payload_bytes),
               "DaemonBootstrapConfig.max_payload_bytes should match v1 default");
  assert_equal(5000, config.dispatch_timeout_ms,
               "DaemonBootstrapConfig.dispatch_timeout_ms should match v1 default");
  assert_equal(3000, config.shutdown_grace_ms,
               "DaemonBootstrapConfig.shutdown_grace_ms should match v1 default");
  assert_equal(3600, config.receipt_ttl_sec,
               "DaemonBootstrapConfig.receipt_ttl_sec should match v1 default");
  assert_equal(1, static_cast<int>(config.accept_workers),
               "DaemonBootstrapConfig.accept_workers should match v1 default");
  assert_equal(4, static_cast<int>(config.dispatch_workers),
               "DaemonBootstrapConfig.dispatch_workers should match v1 default");
  assert_equal(std::string("json"), config.log_format,
               "DaemonBootstrapConfig.log_format should default to json");
  assert_equal(static_cast<int>(DaemonStartupMode::DirectBind),
               static_cast<int>(config.startup_mode),
               "DaemonBootstrapConfig.startup_mode should default to direct bind");
  assert_true(!config.diag_enabled,
              "DaemonBootstrapConfig.diag_enabled should default to disabled");
  assert_true(!config.override_enabled,
              "DaemonBootstrapConfig.override_enabled should default to disabled");
  assert_true(!config.watchdog_enabled,
              "DaemonBootstrapConfig.watchdog_enabled should default to disabled");
  assert_equal(std::string("info"), config.log_level,
               "DaemonBootstrapConfig.log_level should default to info");
  assert_true(config.has_consistent_values(),
              "DaemonBootstrapConfig defaults should be internally consistent");
}

void test_bootstrap_config_rejects_invalid_socket_paths() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::tests::support::assert_true;

  DaemonBootstrapConfig empty_socket_path;
  empty_socket_path.socket_path.clear();
  assert_true(!empty_socket_path.has_consistent_values(),
              "DaemonBootstrapConfig should reject empty socket_path");

  DaemonBootstrapConfig relative_socket_path;
  relative_socket_path.socket_path = "dasall/control.sock";
  assert_true(!relative_socket_path.has_consistent_values(),
              "DaemonBootstrapConfig should reject non-absolute socket_path");
}

void test_bootstrap_config_rejects_invalid_workers_and_windows() {
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::tests::support::assert_true;

  DaemonBootstrapConfig zero_accept_workers;
  zero_accept_workers.accept_workers = 0;
  assert_true(!zero_accept_workers.has_consistent_values(),
              "DaemonBootstrapConfig should reject accept_workers=0");

  DaemonBootstrapConfig zero_dispatch_workers;
  zero_dispatch_workers.dispatch_workers = 0;
  assert_true(!zero_dispatch_workers.has_consistent_values(),
              "DaemonBootstrapConfig should reject dispatch_workers=0");

  DaemonBootstrapConfig invalid_receipt_ttl;
  invalid_receipt_ttl.receipt_ttl_sec = 0;
  assert_true(!invalid_receipt_ttl.has_consistent_values(),
              "DaemonBootstrapConfig should reject non-positive receipt_ttl_sec");

  DaemonBootstrapConfig invalid_shutdown_grace;
  invalid_shutdown_grace.shutdown_grace_ms = -1;
  assert_true(!invalid_shutdown_grace.has_consistent_values(),
              "DaemonBootstrapConfig should reject non-positive shutdown_grace_ms");
}

void test_process_context_and_conflict_types_are_defined() {
  using dasall::apps::daemon::DaemonConfigConflict;
  using dasall::apps::daemon::DaemonConfigSource;
  using dasall::apps::daemon::DaemonProcessContext;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  DaemonConfigConflict conflict;
  conflict.key = "daemon.socket_path";
  conflict.first_source = DaemonConfigSource::CommandLine;
  conflict.second_source = DaemonConfigSource::ConfigFile;
  conflict.first_value = "/tmp/dasall-a.sock";
  conflict.second_value = "/tmp/dasall-b.sock";
  assert_true(conflict.has_consistent_values(),
              "DaemonConfigConflict should capture conflicting sources and values");

  DaemonProcessContext context;
  context.effective_profile_id = "edge_balanced";
  context.config_revision = "daemon-config-v1";
  assert_equal(std::string("edge_balanced"), context.effective_profile_id,
               "DaemonProcessContext should keep effective profile id");
  assert_true(!context.has_consistent_values(),
              "DaemonProcessContext should stay incomplete until build injects runtime handles");
}

}  // namespace

int main() {
  try {
    test_bootstrap_config_defaults_match_v1_design();
    test_bootstrap_config_rejects_invalid_socket_paths();
    test_bootstrap_config_rejects_invalid_workers_and_windows();
    test_process_context_and_conflict_types_are_defined();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonBootstrapConfigTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}