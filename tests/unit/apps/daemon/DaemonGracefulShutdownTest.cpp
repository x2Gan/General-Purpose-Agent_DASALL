#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

#include <unistd.h>

#include "DaemonBootstrap.h"
#include "PlatformError.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::AccessGatewayState;
using dasall::access::IAccessGateway;
using dasall::access::InboundPacket;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchResult;
using dasall::apps::daemon::DaemonBootstrap;
using dasall::apps::daemon::DaemonBootstrapConfig;
using dasall::apps::daemon::DaemonProcessContext;
using dasall::platform::IpcChannelHandle;
using dasall::platform::IpcEndpoint;
using dasall::platform::IpcListenerHandle;
using dasall::platform::IpcPayload;
using dasall::platform::IpcReceiveResult;
using dasall::platform::IpcSendResult;
using dasall::platform::ListenOptions;
using dasall::platform::PeerIdentitySnapshot;
using dasall::platform::PlatformError;
using dasall::platform::PlatformErrorCategory;
using dasall::platform::PlatformErrorCode;
using dasall::platform::PlatformResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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

[[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                       PlatformErrorCategory category,
                                       std::string detail) {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

class BlockingGateway final : public IAccessGateway {
 public:
  bool init() override { return true; }

  RuntimeDispatchResult submit(const InboundPacket&) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      submit_started_ = true;
    }
    cv_.notify_all();

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return allow_submit_exit_; });

    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    PublishEnvelope envelope;
    envelope.request_id = "req-022-graceful";
    envelope.session_id = "sess-022-graceful";
    envelope.trace_id = "trace-022-graceful";
    envelope.result_id = "result-022-graceful";
    envelope.protocol_kind = "unix";
    envelope.protocol_status_hint = "200";
    envelope.payload = "ok";
    result.publish_envelope = std::move(envelope);
    return result;
  }

  bool publish_result(const PublishEnvelope&) override { return true; }

  [[nodiscard]] AccessGatewayState state() const override {
    return AccessGatewayState::Ready;
  }

  [[nodiscard]] bool is_ready() const override { return true; }

  void shutdown(std::chrono::milliseconds drain_timeout) override {
    last_shutdown_timeout_ = drain_timeout;
    ++shutdown_calls_;
  }

  [[nodiscard]] bool wait_for_submit_start(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this]() { return submit_started_; });
  }

  void release_submit() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      allow_submit_exit_ = true;
    }
    cv_.notify_all();
  }

  [[nodiscard]] int shutdown_calls() const { return shutdown_calls_; }
  [[nodiscard]] std::chrono::milliseconds last_shutdown_timeout() const {
    return last_shutdown_timeout_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool submit_started_ = false;
  bool allow_submit_exit_ = false;
  int shutdown_calls_ = 0;
  std::chrono::milliseconds last_shutdown_timeout_{0};
};

class SingleRequestIpc final : public dasall::platform::IIPC {
 public:
  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint&,
                                           const ListenOptions&) override {
    return PlatformResult<IpcListenerHandle>::success(
        IpcListenerHandle{.native_fd = 21U});
  }

  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle&, std::int32_t) override {
    if (!accepted_) {
      accepted_ = true;
      return PlatformResult<IpcChannelHandle>::success(
          IpcChannelHandle{.native_fd = 22U});
    }

    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "accept polling timeout"));
  }

  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "connect is not used by DaemonGracefulShutdownTest"));
  }

  PlatformResult<IpcSendResult> send(const IpcChannelHandle&, const IpcPayload& payload) override {
    last_sent_payload_ = payload;
    return PlatformResult<IpcSendResult>::success(
        IpcSendResult{.bytes_sent = static_cast<std::uint32_t>(payload.size())});
  }

  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle&, std::int32_t) override {
    IpcReceiveResult result;
    const std::string ping_json =
      R"({"schema_version":"1","request_id":"req-022-graceful","command":"ping","args":{},"payload":""})";
    result.data.assign(ping_json.begin(), ping_json.end());
    result.peer_closed = false;
    return PlatformResult<IpcReceiveResult>::success(result);
  }

  PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle&) override {
    PeerIdentitySnapshot snapshot;
    snapshot.peer_uid = 1000;
    snapshot.peer_gid = 1000;
    snapshot.peer_pid = 4242;
    snapshot.is_local_socket_peer = true;
    return PlatformResult<PeerIdentitySnapshot>::success(snapshot);
  }

  PlatformResult<bool> close(const IpcChannelHandle&) override {
    return PlatformResult<bool>::success(true);
  }

 private:
  bool accepted_ = false;
  IpcPayload last_sent_payload_{};
};

void test_daemon_stop_waits_for_inflight_request_to_drain() {
  using namespace std::chrono_literals;

  auto ipc = std::make_shared<SingleRequestIpc>();
  auto gateway = std::make_shared<BlockingGateway>();
  ScopedTempDirectory temp_root("daemon-graceful-shutdown");

  DaemonProcessContext context;
  context.bootstrap_config.socket_path = (temp_root.path() / "daemon-graceful-shutdown.sock").string();
  context.bootstrap_config.shutdown_grace_ms = 200;
  context.effective_profile_id = "daemon.graceful.shutdown";
  context.ipc = ipc;
  context.access_gateway = gateway;
  context.watchdog_service = nullptr;
  context.config_revision = std::nullopt;

  DaemonBootstrap bootstrap;
  bool run_ok = false;
  std::thread daemon_thread([&bootstrap, &context, &run_ok]() {
    run_ok = bootstrap.run(context);
  });

  const auto stop_and_join = [&bootstrap, &daemon_thread]() {
    bootstrap.stop(std::chrono::milliseconds(50));
    if (daemon_thread.joinable()) {
      daemon_thread.join();
    }
  };

  try {
    const bool submit_started = gateway->wait_for_submit_start(500ms);
    assert_true(submit_started,
                "graceful shutdown test should observe submit start before issuing stop");

    std::atomic<bool> stop_finished{false};
    std::thread stop_thread([&bootstrap, &stop_finished]() {
      bootstrap.stop(200ms);
      stop_finished.store(true);
    });

    std::this_thread::sleep_for(20ms);
    assert_true(!stop_finished.load(),
                "graceful stop should wait while an inflight request is still running");

    gateway->release_submit();

    if (stop_thread.joinable()) {
      stop_thread.join();
    }
    if (daemon_thread.joinable()) {
      daemon_thread.join();
    }

    assert_true(run_ok, "daemon run loop should exit cleanly after graceful drain");
    assert_equal(1, gateway->shutdown_calls(),
                 "bootstrap stop should forward one shutdown request to access gateway");
    assert_equal(200, static_cast<int>(gateway->last_shutdown_timeout().count()),
                 "bootstrap stop should forward configured drain timeout");
  } catch (...) {
    gateway->release_submit();
    stop_and_join();
    throw;
  }
}

}  // namespace

int main() {
  try {
    test_daemon_stop_waits_for_inflight_request_to_drain();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonGracefulShutdownTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
