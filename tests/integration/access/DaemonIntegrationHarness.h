#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "CliIpcClient.h"
#include "DaemonBootstrap.h"
#include "PlatformError.h"
#include "daemon/DaemonFrameCodec.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace dasall::tests::integration::access_support {

namespace fs = std::filesystem;

class ScopedTempDirectory {
 public:
  explicit ScopedTempDirectory(std::string_view stem)
      : path_(fs::temp_directory_path() /
              (std::string(stem) + "-" + std::to_string(::getpid()) + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
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

[[nodiscard]] inline dasall::platform::IpcPayload to_ipc_payload(
    std::string_view payload_text) {
  dasall::platform::IpcPayload ipc_payload;
  ipc_payload.reserve(payload_text.size());
  for (const char ch : payload_text) {
    ipc_payload.push_back(static_cast<std::uint8_t>(ch));
  }
  return ipc_payload;
}

inline void populate_response_fields(
    const dasall::access::daemon::UdsResponseFrame& frame,
    dasall::apps::cli::DaemonClientResponse& response) {
  response.request_id = frame.request_id;
  response.trace_id = frame.trace_id;
  response.disposition = frame.disposition;
  response.session_id = frame.session_id;
  response.exit_code_hint = frame.exit_code_hint;
  response.receipt_ref = frame.receipt_ref;
  response.error_ref = frame.error_ref;
  if (frame.agent_result.has_value()) {
    response.response_text = frame.agent_result->response_text;
    response.task_completed = frame.agent_result->task_completed;
  }
}

class DaemonIntegrationHarness {
 public:
  explicit DaemonIntegrationHarness(
      dasall::access::DaemonAccessPipelineOptions options,
      dasall::apps::daemon::DaemonBootstrapConfig config = {},
      std::int32_t connect_deadline_ms = 50)
      : daemon_ipc_(std::make_shared<dasall::platform::linux::UnixIpcProvider>()),
        client_ipc_(std::make_shared<dasall::platform::linux::UnixIpcProvider>()),
        temp_root_("daemon-integration"),
        connect_deadline_ms_(connect_deadline_ms) {
    if (options.bootstrap_config.allowed_protocols.empty()) {
      options.bootstrap_config.allowed_protocols = {"ipc_uds"};
    }
    if (options.daemon_profile_id.empty()) {
      options.daemon_profile_id = "daemon.integration";
    }

    profile_id_ = options.daemon_profile_id;
    gateway_ = dasall::access::create_daemon_access_gateway(std::move(options));
    dasall::tests::support::assert_true(
        gateway_ != nullptr,
        "daemon integration harness should build a real access gateway");
    dasall::tests::support::assert_true(
        gateway_->init(),
        "daemon integration harness gateway should initialize");

    const auto default_socket_path =
      dasall::apps::daemon::DaemonBootstrapConfig{}.socket_path;
    if (config.socket_path.empty() || config.socket_path == default_socket_path) {
      config.socket_path = (temp_root_.path() / "control.sock").string();
    }
    endpoint_.socket_path = config.socket_path;

    context_ = dasall::apps::daemon::DaemonBootstrap::build(
        config,
        dasall::apps::daemon::DaemonBootstrap::BuildDependencies{
            .ipc = daemon_ipc_,
            .access_gateway = gateway_,
            .watchdog_service = nullptr,
            .effective_profile_id = profile_id_,
            .config_revision = std::nullopt,
        });
    dasall::tests::support::assert_true(
        context_.has_value(),
        "daemon integration harness should build a process context before start");

    start();
  }

  ~DaemonIntegrationHarness() {
    stop();
  }

  DaemonIntegrationHarness(const DaemonIntegrationHarness&) = delete;
  DaemonIntegrationHarness& operator=(const DaemonIntegrationHarness&) = delete;
  DaemonIntegrationHarness(DaemonIntegrationHarness&&) = delete;
  DaemonIntegrationHarness& operator=(DaemonIntegrationHarness&&) = delete;

  void start() {
    if (started_) {
      return;
    }

    run_ok_ = false;
    daemon_thread_ = std::thread([this]() {
      run_ok_ = bootstrap_.run(*context_);
    });
    try {
      wait_until_ready();
    } catch (...) {
      bootstrap_.stop();
      if (daemon_thread_.joinable()) {
        daemon_thread_.join();
      }
      throw;
    }
    started_ = true;
  }

  void stop(
      const std::chrono::milliseconds drain_timeout = std::chrono::milliseconds::zero()) {
    if (stopped_) {
      return;
    }

    bootstrap_.stop(drain_timeout);
    if (daemon_thread_.joinable()) {
      daemon_thread_.join();
    }
    stopped_ = true;
  }

  [[nodiscard]] bool daemon_stopped_cleanly() const {
    return run_ok_;
  }

  [[nodiscard]] std::size_t active_connection_count() const {
    return bootstrap_.active_connection_count();
  }

  [[nodiscard]] dasall::apps::cli::CliIpcClient make_client() const {
    return dasall::apps::cli::CliIpcClient(
        client_ipc_, endpoint_, connect_deadline_ms_);
  }

  [[nodiscard]] const std::string& socket_path() const {
    return endpoint_.socket_path;
  }

  [[nodiscard]] dasall::apps::cli::DaemonClientResponse send_frame(
      const dasall::access::daemon::UdsRequestFrame& frame) const {
    return send_frame(frame, connect_deadline_ms_);
  }

  [[nodiscard]] dasall::apps::cli::DaemonClientResponse send_frame(
      const dasall::access::daemon::UdsRequestFrame& frame,
      const std::int32_t deadline_ms) const {
    dasall::apps::cli::DaemonClientResponse response;

    const auto channel = client_ipc_->connect(endpoint_, deadline_ms);
    if (!channel.ok() || !channel.value.has_value()) {
      response.failure_reason = channel.error.has_value()
                                    ? channel.error->detail
                                    : "daemon connect failed";
      return response;
    }

    const auto request_text = dasall::access::daemon::encode_request_frame(frame);
    const auto sent = client_ipc_->send(*channel.value, to_ipc_payload(request_text));
    if (!sent.ok()) {
      (void)client_ipc_->close(*channel.value);
      response.failure_reason = sent.error.has_value()
                                    ? sent.error->detail
                                    : "daemon send failed";
      return response;
    }

    const auto received = client_ipc_->receive(*channel.value, deadline_ms);
    (void)client_ipc_->close(*channel.value);
    if (!received.ok() || !received.value.has_value()) {
      response.failure_reason = received.error.has_value()
                                    ? received.error->detail
                                    : "daemon receive failed";
      return response;
    }

    response.transport_ok = true;
    response.peer_closed = received.value->peer_closed;
    response.raw_response.assign(
        reinterpret_cast<const char*>(received.value->data.data()),
        received.value->data.size());
    if (response.peer_closed) {
      response.failure_reason = "daemon closed channel before returning a response";
      return response;
    }
    if (response.raw_response.empty()) {
      response.failure_reason = "daemon returned an empty response frame";
      return response;
    }

    const auto decoded =
        dasall::access::daemon::decode_response_frame(response.raw_response);
    if (!decoded.ok()) {
      response.failure_reason = "daemon returned an invalid response frame";
      return response;
    }

    response.parse_ok = true;
    populate_response_fields(decoded.frame, response);
    return response;
  }

 private:
  void wait_until_ready() const {
    using namespace std::chrono_literals;

    std::string last_error = "daemon listener did not accept connections before timeout";
    for (int attempt = 0; attempt < 100; ++attempt) {
      const auto channel = client_ipc_->connect(endpoint_, 10);
      if (channel.ok() && channel.value.has_value()) {
        (void)client_ipc_->close(*channel.value);
        return;
      }
      if (channel.error.has_value() && !channel.error->detail.empty()) {
        last_error = channel.error->detail;
      }
      std::this_thread::sleep_for(20ms);
    }

    dasall::tests::support::assert_true(
        false,
        "daemon integration harness should connect once daemon listener is bound: " +
            last_error);
  }

  std::shared_ptr<dasall::platform::linux::UnixIpcProvider> daemon_ipc_;
  std::shared_ptr<dasall::platform::linux::UnixIpcProvider> client_ipc_;
  std::shared_ptr<dasall::access::IAccessGateway> gateway_;
  ScopedTempDirectory temp_root_;
  std::string profile_id_;
  std::int32_t connect_deadline_ms_ = 50;
  dasall::platform::IpcEndpoint endpoint_;
  std::optional<dasall::apps::daemon::DaemonProcessContext> context_;
  dasall::apps::daemon::DaemonBootstrap bootstrap_;
  mutable std::thread daemon_thread_;
  bool run_ok_ = false;
  bool started_ = false;
  bool stopped_ = false;
};

}  // namespace dasall::tests::integration::access_support