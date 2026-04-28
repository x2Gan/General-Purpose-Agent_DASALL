#include <csignal>
#include <exception>
#include <iostream>

#include "DaemonSignalHandler.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonSignalHandler;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_sigterm_requests_shutdown() {
  DaemonSignalHandler signal_handler;
  assert_true(signal_handler.install_handlers(),
              "install_handlers should succeed for SIGTERM test");

  std::raise(SIGTERM);

  assert_true(signal_handler.shutdown_requested(),
              "SIGTERM should set shutdown intent");
  assert_true(!signal_handler.reload_requested(),
              "SIGTERM should not set reload intent");
  assert_equal(SIGTERM, signal_handler.last_signal(),
               "SIGTERM should be recorded as last_signal");
}

void test_sigint_requests_shutdown() {
  DaemonSignalHandler signal_handler;
  assert_true(signal_handler.install_handlers(),
              "install_handlers should succeed for SIGINT test");

  std::raise(SIGINT);

  assert_true(signal_handler.shutdown_requested(),
              "SIGINT should set shutdown intent");
  assert_true(!signal_handler.reload_requested(),
              "SIGINT should not set reload intent");
  assert_equal(SIGINT, signal_handler.last_signal(),
               "SIGINT should be recorded as last_signal");
}

void test_sighup_requests_reload_only() {
  DaemonSignalHandler signal_handler;
  assert_true(signal_handler.install_handlers(),
              "install_handlers should succeed for SIGHUP test");

  std::raise(SIGHUP);

  assert_true(!signal_handler.shutdown_requested(),
              "SIGHUP should not set shutdown intent");
  assert_true(signal_handler.reload_requested(),
              "SIGHUP should set reload intent");
  assert_equal(SIGHUP, signal_handler.last_signal(),
               "SIGHUP should be recorded as last_signal");
}

}  // namespace

int main() {
  try {
    test_sigterm_requests_shutdown();
    test_sigint_requests_shutdown();
    test_sighup_requests_reload_only();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonSignalHandlerTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}