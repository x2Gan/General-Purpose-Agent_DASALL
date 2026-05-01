#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "DaemonBootstrap.h"
#include "PlatformError.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string_view stem)
      : path_(fs::temp_directory_path() /
              (std::string(stem) + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    fs::create_directories(path_);
    fs::permissions(path_, static_cast<fs::perms>(0700), fs::perm_options::replace);
  }

  ~ScopedTempDirectory() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

std::shared_ptr<dasall::access::IAccessGateway> build_gateway() {
  using dasall::access::DaemonAccessPipelineOptions;
  using dasall::tests::support::assert_true;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_version = "v1";
  options.daemon_profile_id = "daemon.ping.integration";
  options.runtime_dispatch_backend = [](const auto&) {
    return dasall::access::RuntimeDispatchResult{};
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "daemon ping integration should build a real access gateway");
  assert_true(gateway->init(),
              "daemon ping integration gateway should initialize");
  return gateway;
}

void test_daemon_ping_roundtrip_returns_response_payload() {
  using namespace std::chrono_literals;

  using dasall::apps::daemon::DaemonBootstrap;
  using dasall::apps::daemon::DaemonBootstrapConfig;
  using dasall::platform::IpcEndpoint;
  using dasall::platform::IpcPayload;
  using dasall::platform::linux::UnixIpcProvider;
  using dasall::tests::support::assert_true;

  ScopedTempDirectory temp_root("daemon-ping-integration");
  auto daemon_ipc = std::make_shared<UnixIpcProvider>();
  auto client_ipc = std::make_shared<UnixIpcProvider>();
  auto gateway = build_gateway();

  DaemonBootstrapConfig config;
  config.socket_path = (temp_root.path() / "control.sock").string();

  IpcEndpoint endpoint;
  endpoint.socket_path = config.socket_path;

  const auto context = DaemonBootstrap::build(
      config,
      DaemonBootstrap::BuildDependencies{
          .ipc = daemon_ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.ping.integration",
          .config_revision = std::nullopt,
      });
  assert_true(context.has_value(),
              "daemon ping integration should build a process context before run(context)");

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

    std::optional<dasall::platform::IpcChannelHandle> client_channel;
    for (int attempt = 0; attempt < 20; ++attempt) {
      const auto client = client_ipc->connect(endpoint, 10);
      if (client.ok() && client.value.has_value()) {
        client_channel = *client.value;
        break;
      }
      std::this_thread::sleep_for(10ms);
    }
    assert_true(client_channel.has_value(),
                "daemon ping integration client should connect once daemon listener is bound");

    const std::string ping_json =
        R"({"schema_version":"1","request_id":"ping-itg-001","command":"ping"})";
    IpcPayload payload;
    payload.reserve(ping_json.size());
    for (const char ch : ping_json) {
      payload.push_back(static_cast<std::uint8_t>(ch));
    }

    const auto send_result = client_ipc->send(*client_channel, payload);
    assert_true(send_result.ok(), "daemon ping integration should send ping payload successfully");

    std::string response_text;
    bool received_response = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
      const auto receive_result = client_ipc->receive(*client_channel, 10);
      if (!receive_result.ok()) {
        assert_true(receive_result.error.has_value() &&
                        receive_result.error->code ==
                            dasall::platform::PlatformErrorCode::Timeout,
                    "daemon ping integration polling should only observe timeout before response arrives");
        std::this_thread::sleep_for(10ms);
        continue;
      }

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
                "daemon ping integration should receive daemon response payload");
    assert_true(response_text.find("\"disposition\":\"completed\"") !=
                    std::string::npos,
                "daemon ping integration should surface completed disposition");
    assert_true(response_text.find("\\\"daemon_version\\\":\\\"v1\\\"") !=
                    std::string::npos,
                "daemon ping integration should surface daemon version in response payload");
    assert_true(response_text.find("\\\"readiness\\\":\\\"READY\\\"") !=
                    std::string::npos,
                "daemon ping integration should surface readiness summary in response payload");

    stop_and_join();
    assert_true(run_ok, "daemon ping integration daemon thread should stop cleanly");
  } catch (...) {
    stop_and_join();
    throw;
  }
}

}  // namespace

int main() {
  try {
    test_daemon_ping_roundtrip_returns_response_payload();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonPingIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}