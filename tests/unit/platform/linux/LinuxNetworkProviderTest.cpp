#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/LinuxNetworkProvider.h"

namespace {

void test_linux_network_provider_reports_timeout_and_connection_refused() {
  using dasall::platform::ConnectOptions;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::SocketEndpoint;
  using dasall::platform::linux::LinuxNetworkProvider;
  using dasall::tests::support::assert_true;

  LinuxNetworkProvider provider;

  SocketEndpoint timeout_endpoint;
  timeout_endpoint.host = "timeout-host";
  timeout_endpoint.port = 8080;
  ConnectOptions timeout_options;
  timeout_options.connect_timeout_ms = 0;

  const auto timeout_result = provider.connect(timeout_endpoint, timeout_options);
  assert_true(!timeout_result.ok(), "connect should fail when connect timeout is zero");
  assert_true(timeout_result.error->code == PlatformErrorCode::Timeout,
              "zero connect timeout should map to Timeout");

  SocketEndpoint refused_endpoint;
  refused_endpoint.host = "refused-host";
  refused_endpoint.port = 8081;
  ConnectOptions default_options;

  const auto refused_result = provider.connect(refused_endpoint, default_options);
  assert_true(!refused_result.ok(), "connect should fail for refused endpoint pattern");
  assert_true(refused_result.error->code == PlatformErrorCode::ConnectionRefused,
              "refused endpoint should map to ConnectionRefused");
}

void test_linux_network_provider_reports_disconnected_and_fallback_backend() {
  using dasall::platform::ConnectOptions;
  using dasall::platform::NetworkBuffer;
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::SocketEndpoint;
  using dasall::platform::linux::LinuxNetworkProvider;
  using dasall::platform::linux::NetworkIoBackend;
  using dasall::tests::support::assert_true;

  LinuxNetworkProvider provider;
  provider.set_enable_epoll(true);

  SocketEndpoint endpoint;
  endpoint.host = "needs-fallback";
  endpoint.port = 9000;

  ConnectOptions options;
  const auto connected = provider.connect(endpoint, options);
  assert_true(connected.ok(), "connect should succeed for regular endpoint");
  assert_true(provider.last_backend() == NetworkIoBackend::Poll,
              "fallback trigger should switch backend to poll");

  const auto shut = provider.shutdown(*connected.value);
  assert_true(shut.ok(), "shutdown should succeed for active connection");

  const NetworkBuffer data{1U, 2U};
  const auto send_after_shutdown = provider.send(*connected.value, data, 10);
  assert_true(!send_after_shutdown.ok(), "send should fail after shutdown");
  assert_true(send_after_shutdown.error->code == PlatformErrorCode::Disconnected,
              "send after shutdown should map to Disconnected");
}

}  // namespace

int main() {
  try {
    test_linux_network_provider_reports_timeout_and_connection_refused();
    test_linux_network_provider_reports_disconnected_and_fallback_backend();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}