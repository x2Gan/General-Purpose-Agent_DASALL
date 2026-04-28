#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "DaemonBootstrap.h"
#include "IAccessGateway.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

class ReadyGateway final : public dasall::access::IAccessGateway {
 public:
  bool init() override { return true; }

  dasall::access::RuntimeDispatchResult submit(
      const dasall::access::InboundPacket&) override {
    return dasall::access::RuntimeDispatchResult{};
  }

  bool publish_result(const dasall::access::PublishEnvelope&) override {
    return true;
  }

  [[nodiscard]] dasall::access::AccessGatewayState state() const override {
    return dasall::access::AccessGatewayState::Ready;
  }

  [[nodiscard]] bool is_ready() const override { return true; }

  void shutdown(std::chrono::milliseconds) override {}
};

void test_daemon_loopback_fixture_consumes_request_and_returns_response() {
  using namespace std::chrono_literals;

  using dasall::apps::daemon::DaemonBootstrap;
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::IpcPayload;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<UnixIpcProvider>();
  auto gateway = std::make_shared<ReadyGateway>();
  DaemonBootstrapConfig config;
  config.socket_path = "/tmp/daemon-loopback.sock";

  IpcEndpoint endpoint;
  endpoint.socket_path = config.socket_path;

  const auto context = DaemonBootstrap::build(
      config,
      DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.loopback.fixture",
          .config_revision = std::nullopt,
      });
  assert_true(context.has_value(),
              "daemon loopback fixture should build a process context before run(context)");

  DaemonBootstrap bootstrap;

  bool run_ok = false;
  std::thread daemon_thread([&bootstrap, &context, &run_ok]() {
    run_ok = bootstrap.run(*context);
  });

  const auto stop_and_join = [&bootstrap, &daemon_thread]() {
    bootstrap.stop();
    if (daemon_thread.joinable()) {
      daemon_thread.join();
    }
  };

  try {
    std::this_thread::sleep_for(20ms);

    const auto client = ipc->connect(endpoint, 10);
    assert_true(client.ok(), "client should connect to daemon loopback listener");

    const std::string ping_json = R"({"op":"ping"})";
    IpcPayload payload;
    payload.reserve(ping_json.size());
    for (const char ch : ping_json) {
      payload.push_back(static_cast<std::uint8_t>(ch));
    }

    const auto send_result = ipc->send(*client.value, payload);
    assert_true(send_result.ok(), "client should send ping payload through loopback channel");

    std::string response_text;
    bool received_response = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
      const auto receive_result = ipc->receive(*client.value, 10);
      assert_true(receive_result.ok(), "client receive should not fail while polling loopback response");
      if (!receive_result.value->data.empty()) {
        response_text.assign(
            reinterpret_cast<const char*>(receive_result.value->data.data()),
            receive_result.value->data.size());
        received_response = true;
        break;
      }

      std::this_thread::sleep_for(10ms);
    }

    assert_true(received_response,
                "daemon loopback fixture should expose response payload to client");
    assert_true(response_text.find("\"status\":\"ok\"") != std::string::npos,
                "daemon ping response should contain ok status");
    assert_true(response_text.find("dasall-daemon") != std::string::npos,
                "daemon ping response should surface daemon service name");

    stop_and_join();
    assert_true(run_ok, "daemon bootstrap loopback fixture should stop cleanly");
  } catch (...) {
    stop_and_join();
    throw;
  }
}

}  // namespace

int main() {
  try {
    test_daemon_loopback_fixture_consumes_request_and_returns_response();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonLoopbackFixtureTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}