#include <exception>
#include <iostream>

#include "DaemonInProcessFixture.h"
#include "DaemonSocketIntegrationSupport.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonBootstrapConfig;
using dasall::tests::integration::access_support::DaemonInProcessFixture;
using dasall::tests::integration::access_support::SocketScopedTempDirectory;
using dasall::tests::integration::access_support::bind_native_socket;
using dasall::tests::integration::access_support::make_ping_only_options;
using dasall::tests::integration::access_support::socket_mode_bits;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_daemon_recovers_from_owned_stale_socket_left_by_previous_run() {
  SocketScopedTempDirectory temp_root("daemon-stale-recovery-itg");
  const auto socket_path = temp_root.path() / "control.sock";

  auto stale_socket = bind_native_socket(socket_path, 0600, false);
  stale_socket.close_keep_path();
  assert_true(std::filesystem::exists(socket_path),
              "stale socket recovery integration should start with a leftover socket path");

  DaemonBootstrapConfig config;
  config.socket_path = socket_path.string();

  DaemonInProcessFixture fixture(
      make_ping_only_options("daemon.stale-recovery.integration"),
      config,
      100);

  assert_equal(0600,
               static_cast<int>(socket_mode_bits(socket_path)),
               "daemon should recreate the recovered socket with policy-compliant mode");

  const auto response = fixture.make_client().ping_daemon();
  assert_true(response.ok() && response.is_completed(),
              "stale socket recovery integration should restore daemon ping after cleanup");

  fixture.stop();
  assert_true(fixture.daemon_stopped_cleanly(),
              "stale socket recovery integration should stop daemon cleanly after restart");
}

}  // namespace

int main() {
  try {
    test_daemon_recovers_from_owned_stale_socket_left_by_previous_run();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonStaleSocketRecoveryIntegrationTest] FAILED: " << ex.what()
              << '\n';
    return 1;
  }

  return 0;
}