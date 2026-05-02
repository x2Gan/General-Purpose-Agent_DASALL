#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "AccessGatewayFactory.h"
#include "CliCommandParser.h"
#include "CliIpcClient.h"
#include "DaemonBootstrap.h"
#include "DaemonSocketPolicy.h"
#include "daemon/DaemonEndpointDefaults.h"
#include "linux/UnixIpcProvider.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::access::DaemonAccessPipelineOptions;
using dasall::apps::cli::CliCommand;
using dasall::apps::cli::CliCommandParser;
using dasall::apps::cli::CliIpcClient;
using dasall::apps::daemon::DaemonBootstrap;
using dasall::apps::daemon::DaemonBootstrapConfig;
using dasall::apps::daemon::DaemonSocketPolicy;
using dasall::platform::IpcEndpoint;
using dasall::platform::linux::UnixIpcProvider;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool socket_path_is_active(const fs::path& socket_path) {
  const int probe_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  assert_true(probe_fd >= 0,
              "socket-path integration should create a unix probe socket");

  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  const std::string raw_path = socket_path.string();
  std::strncpy(address.sun_path, raw_path.c_str(), sizeof(address.sun_path) - 1U);

  const bool connected =
      ::connect(probe_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
  (void)::close(probe_fd);
  return connected;
}

void cleanup_owned_inactive_socket(const fs::path& socket_path) {
  struct stat socket_stat {};
  if (::lstat(socket_path.c_str(), &socket_stat) != 0) {
    return;
  }

  assert_true(S_ISSOCK(socket_stat.st_mode),
              "socket-path integration should only reuse unix socket paths");
  assert_true(static_cast<std::uint32_t>(socket_stat.st_uid) ==
                      static_cast<std::uint32_t>(::getuid()) &&
                  static_cast<std::uint32_t>(socket_stat.st_gid) ==
                      static_cast<std::uint32_t>(::getgid()),
              "socket-path integration should only recycle sockets owned by the current process user");
  assert_true(!socket_path_is_active(socket_path),
              "socket-path integration should not remove an active daemon socket");

  const auto removed = fs::remove(socket_path);
  assert_true(removed,
              "socket-path integration should remove inactive owned socket leftovers before bind");
}

class ScopedSocketPath {
 public:
  explicit ScopedSocketPath(fs::path socket_path, bool remove_parent_on_exit = false)
      : socket_path_(std::move(socket_path)),
        parent_path_(socket_path_.parent_path()),
        remove_parent_on_exit_(remove_parent_on_exit) {
    cleanup_owned_inactive_socket(socket_path_);

    IpcEndpoint endpoint;
    endpoint.socket_path = socket_path_.string();
    const auto preflight = dasall::apps::daemon::preflight_bind_endpoint(
        endpoint, DaemonSocketPolicy::for_current_process());
    assert_true(preflight.ok(),
          "socket-path integration should preflight daemon bind path before start: " +
            (preflight.error.has_value() ? preflight.error->detail : std::string("unknown error")));
  }

  ~ScopedSocketPath() {
    std::error_code error;
    fs::remove(socket_path_, error);
    if (remove_parent_on_exit_) {
      fs::remove_all(parent_path_, error);
    }
  }

  [[nodiscard]] const fs::path& path() const {
    return socket_path_;
  }

 private:
  fs::path socket_path_;
  fs::path parent_path_;
  bool remove_parent_on_exit_ = false;
};

[[nodiscard]] std::shared_ptr<dasall::access::IAccessGateway> build_gateway(
    const std::string& profile_id) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {
      "local://uid/" + std::to_string(static_cast<unsigned int>(::getuid()))};
  options.daemon_profile_id = profile_id;
  options.daemon_version = "v1";
  options.runtime_dispatch_backend = [profile_id](const auto& request) {
    dasall::access::RuntimeDispatchResult result;
    result.disposition = dasall::access::AccessDisposition::Completed;

    dasall::access::PublishEnvelope envelope;
    envelope.request_id = request.request_context.at("request_id");
    envelope.trace_id = request.request_context.contains("trace_id")
                            ? request.request_context.at("trace_id")
                            : envelope.request_id + "-trace";
    envelope.protocol_kind = request.packet.protocol_kind;
    envelope.protocol_status_hint = "200";

    dasall::contracts::AgentResult agent_result;
    agent_result.request_id = envelope.request_id;
    agent_result.response_text = "profile=" + profile_id + "; READY";
    agent_result.task_completed = true;
    envelope.agent_result = std::move(agent_result);

    result.publish_envelope = std::move(envelope);
    return result;
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr,
              "socket-path integration should build a daemon access gateway");
  assert_true(gateway->init(),
              "socket-path integration should initialize daemon access gateway");
  return gateway;
}

class RunningDaemon {
 public:
  RunningDaemon(std::string socket_path, std::string profile_id)
      : daemon_ipc_(std::make_shared<UnixIpcProvider>()),
        client_ipc_(std::make_shared<UnixIpcProvider>()),
        socket_path_(std::move(socket_path)) {
    DaemonBootstrapConfig config;
    config.socket_path = socket_path_;

    endpoint_.socket_path = socket_path_;
    context_ = DaemonBootstrap::build(
        config,
        DaemonBootstrap::BuildDependencies{
            .ipc = daemon_ipc_,
            .access_gateway = build_gateway(std::move(profile_id)),
            .watchdog_service = nullptr,
            .effective_profile_id = "daemon.cli.socket-path",
            .config_revision = std::nullopt,
        });
    assert_true(context_.has_value(),
                "socket-path integration should build daemon process context");

    daemon_thread_ = std::thread([this]() {
      run_ok_ = bootstrap_.run(*context_);
    });
    wait_until_ready();
  }

  ~RunningDaemon() {
    stop();
  }

  [[nodiscard]] CliIpcClient make_client(const std::string& socket_path) const {
    IpcEndpoint endpoint;
    endpoint.socket_path = socket_path;
    return CliIpcClient(client_ipc_, endpoint, 100);
  }

  void stop() {
    if (stopped_) {
      return;
    }

    bootstrap_.stop();
    if (daemon_thread_.joinable()) {
      daemon_thread_.join();
    }
    stopped_ = true;
  }

  [[nodiscard]] bool stopped_cleanly() const {
    return run_ok_;
  }

 private:
  void wait_until_ready() const {
    using namespace std::chrono_literals;

    std::string last_error = "daemon listener did not become reachable";
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

    assert_true(false,
                "socket-path integration should connect once daemon listener is bound: " +
                    last_error);
  }

  std::shared_ptr<UnixIpcProvider> daemon_ipc_;
  std::shared_ptr<UnixIpcProvider> client_ipc_;
  std::string socket_path_;
  IpcEndpoint endpoint_;
  std::optional<dasall::apps::daemon::DaemonProcessContext> context_;
  mutable std::thread daemon_thread_;
  mutable DaemonBootstrap bootstrap_;
  bool run_ok_ = false;
  bool stopped_ = false;
};

[[nodiscard]] std::string resolve_socket_path(const CliCommand& command) {
  return command.socket_path.value_or(
      dasall::access::daemon::kDefaultDaemonSocketPath);
}

void test_cli_default_socket_path_roundtrip_uses_shared_default() {
  const char* argv[] = {"dasall_cli", "ping"};
  const auto command = CliCommandParser::parse(2, argv);
  assert_true(command.has_value(),
              "default socket-path integration should parse ping command without override");
  assert_true(!command->socket_path.has_value(),
              "default socket-path integration should defer to shared fallback constant");

  ScopedSocketPath scoped_path(
      fs::path(dasall::access::daemon::kDefaultDaemonSocketPath));
  RunningDaemon daemon(resolve_socket_path(*command), "daemon.cli.default");

  const auto response = daemon.make_client(resolve_socket_path(*command)).ping_daemon();
  assert_true(response.ok() && response.is_completed(),
              "default socket-path integration should let CLI ping daemon over shared default path");
  assert_true(response.response_text.has_value(),
              "default socket-path integration should surface daemon response payload");
  assert_true(response.response_text->find("READY") != std::string::npos,
              "default socket-path integration should preserve readiness summary");

  daemon.stop();
  assert_true(daemon.stopped_cleanly(),
              "default socket-path integration should stop daemon cleanly");
}

void test_cli_socket_path_override_roundtrip_reaches_custom_endpoint() {
  const fs::path socket_path =
      fs::temp_directory_path() /
      (std::string("dasall-cli-socket-") + std::to_string(::getpid())) /
      "control.sock";
  const std::string socket_path_text = socket_path.string();
  const char* argv[] = {"dasall_cli", "--socket-path", socket_path_text.c_str(), "ping"};
  const auto command = CliCommandParser::parse(4, argv);
  assert_true(command.has_value(),
              "socket-path override integration should parse explicit override option");
  assert_true(command->socket_path.has_value(),
              "socket-path override integration should preserve custom path");
  assert_equal(socket_path_text, *command->socket_path,
               "socket-path override integration should pass custom path through parser");

  ScopedSocketPath scoped_path(socket_path, true);
  RunningDaemon daemon(resolve_socket_path(*command), "daemon.cli.override");

  const auto response = daemon.make_client(resolve_socket_path(*command)).ping_daemon();
  assert_true(response.ok() && response.is_completed(),
              "socket-path override integration should let CLI ping daemon over custom path");
  assert_true(response.response_text.has_value(),
              "socket-path override integration should surface daemon response payload");
  assert_true(response.response_text->find("READY") != std::string::npos,
              "socket-path override integration should preserve readiness summary");

  daemon.stop();
  assert_true(daemon.stopped_cleanly(),
              "socket-path override integration should stop daemon cleanly");
}

}  // namespace

int main() {
  try {
    test_cli_default_socket_path_roundtrip_uses_shared_default();
    test_cli_socket_path_override_roundtrip_reaches_custom_endpoint();
  } catch (const std::exception& ex) {
    std::cerr << "[CliDaemonSocketPathIntegrationTest] FAILED: " << ex.what()
              << '\n';
    return 1;
  }

  return 0;
}