#include <exception>
#include <iostream>

#include "DaemonInProcessFixture.h"
#include "DaemonSocketIntegrationSupport.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonBootstrapConfig;
using dasall::tests::integration::access_support::DaemonInProcessFixture;
using dasall::tests::integration::access_support::SocketScopedTempDirectory;
using dasall::tests::integration::access_support::make_ping_only_options;
using dasall::tests::integration::access_support::socket_mode_bits;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_daemon_creates_policy_compliant_socket_mode() {
  SocketScopedTempDirectory temp_root("daemon-socket-mode-itg");
  const auto socket_path = temp_root.path() / "control.sock";

  DaemonBootstrapConfig config;
  config.socket_path = socket_path.string();

  DaemonInProcessFixture fixture(
      make_ping_only_options("daemon.socket-mode.integration"),
      config,
      100);

  assert_equal(0600,
               static_cast<int>(socket_mode_bits(socket_path)),
               "daemon should create a real socket whose mode matches the security policy");

  const auto response = fixture.make_client().ping_daemon();
  assert_true(response.ok() && response.is_completed(),
              "socket mode integration should keep daemon ping roundtrip available");

  fixture.stop();
  assert_true(fixture.daemon_stopped_cleanly(),
              "socket mode integration should stop daemon cleanly");
  assert_true(!std::filesystem::exists(socket_path),
              "socket mode integration should unlink the socket path during clean stop");
}

}  // namespace

int main() {
  try {
    test_daemon_creates_policy_compliant_socket_mode();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonSocketModeIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}