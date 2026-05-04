#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "AccessGatewayFactory.h"
#include "DaemonBootstrap.h"
#include "DaemonIntegrationHarness.h"
#include "PlatformError.h"
#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::AccessGatewayState;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::IAccessGateway;
using dasall::access::InboundPacket;
using dasall::access::PublishEnvelope;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::apps::daemon::DaemonBootstrap;
using dasall::apps::daemon::DaemonBootstrapConfig;
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
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
using dasall::tests::integration::access_support::ScopedTempDirectory;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] PlatformError make_error(PlatformErrorCode code,
                                       PlatformErrorCategory category,
                                       std::string detail) {
  return PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = "daemon-failure-test",
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

class ReadyGateway final : public IAccessGateway {
 public:
  bool init() override { return true; }

  RuntimeDispatchResult submit(const InboundPacket&) override {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    PublishEnvelope envelope;
    envelope.request_id = "daemon-failure-ready-gateway";
    envelope.protocol_status_hint = "200";
    result.publish_envelope = std::move(envelope);
    return result;
  }

  bool publish_result(const PublishEnvelope&) override { return true; }
  [[nodiscard]] AccessGatewayState state() const override {
    return AccessGatewayState::Ready;
  }
  [[nodiscard]] bool is_ready() const override { return true; }
  void shutdown(std::chrono::milliseconds) override {}
};

class BindConflictIpc final : public dasall::platform::IIPC {
 public:
  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint&,
                                           const ListenOptions&) override {
    return PlatformResult<IpcListenerHandle>::failure(make_error(
        PlatformErrorCode::AddressInUse,
        PlatformErrorCategory::IPC,
        "bind conflict injected by test"));
  }

  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "accept unused in bind conflict test"));
  }

  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "connect unused in bind conflict test"));
  }

  PlatformResult<IpcSendResult> send(const IpcChannelHandle&, const IpcPayload&) override {
    return PlatformResult<IpcSendResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "send unused in bind conflict test"));
  }

  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle&, std::int32_t) override {
    return PlatformResult<IpcReceiveResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "receive unused in bind conflict test"));
  }

  PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle&) override {
    return PlatformResult<PeerIdentitySnapshot>::failure(make_error(
      PlatformErrorCode::InternalFailure,
      PlatformErrorCategory::Internal,
        "describe_peer unused in bind conflict test"));
  }

  PlatformResult<bool> close(const IpcChannelHandle&) override {
    return PlatformResult<bool>::success(true);
  }
};

class UnsupportedPeerIpc final : public dasall::platform::IIPC {
 public:
  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint&,
                                           const ListenOptions&) override {
    return PlatformResult<IpcListenerHandle>::success(
        IpcListenerHandle{.native_fd = 51U});
  }

  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle&, std::int32_t) override {
    if (!accepted_) {
      accepted_ = true;
      return PlatformResult<IpcChannelHandle>::success(IpcChannelHandle{.native_fd = 52U});
    }

    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "accept polling timeout"));
  }

  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "connect unused in unsupported peer test"));
  }

  PlatformResult<IpcSendResult> send(const IpcChannelHandle&, const IpcPayload& payload) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_sent_payload_ = payload;
      sent_ = true;
    }
    cv_.notify_all();
    return PlatformResult<IpcSendResult>::success(
        IpcSendResult{.bytes_sent = static_cast<std::uint32_t>(payload.size())});
  }

  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle&, std::int32_t) override {
    IpcReceiveResult result;
    const auto request_text = dasall::access::daemon::encode_request_frame(
        dasall::access::daemon::UdsRequestFrame{
            .request_id = "cli-run",
            .trace_id = "cli-run-trace",
        .session_hint = std::nullopt,
        .idempotency_key = std::nullopt,
            .command = "run",
            .args = {},
            .payload = "peer identity unsupported",
            .async_preference = dasall::access::daemon::DaemonAsyncPreference::PreferSync,
        .output_mode = dasall::access::daemon::DaemonOutputMode::Human,
        .deadline_ms = std::nullopt,
        });
    result.data.assign(request_text.begin(), request_text.end());
    result.peer_closed = false;
    return PlatformResult<IpcReceiveResult>::success(result);
  }

  PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle&) override {
    return PlatformResult<PeerIdentitySnapshot>::failure(make_error(
      PlatformErrorCode::InternalFailure,
      PlatformErrorCategory::Internal,
        "peer identity unsupported in failure injection test"));
  }

  PlatformResult<bool> close(const IpcChannelHandle&) override {
    return PlatformResult<bool>::success(true);
  }

  [[nodiscard]] bool wait_for_response(std::chrono::milliseconds timeout,
                                       std::string* response_text) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this]() { return sent_; })) {
      return false;
    }
    response_text->assign(reinterpret_cast<const char*>(last_sent_payload_.data()),
                          last_sent_payload_.size());
    return true;
  }

 private:
  bool accepted_ = false;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool sent_ = false;
  IpcPayload last_sent_payload_{};
};

void bind_conflict_returns_failed_startup() {
  auto ipc = std::make_shared<BindConflictIpc>();
  auto gateway = std::make_shared<ReadyGateway>();
  ScopedTempDirectory temp_root("daemon-bind-conflict");

  DaemonBootstrapConfig config;
  config.socket_path = (temp_root.path() / "control.sock").string();

  const auto context = DaemonBootstrap::build(
      config,
      DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.failure.bind_conflict",
          .config_revision = std::nullopt,
      });
  assert_true(context.has_value(),
              "bind conflict failure injection should still build a process context");

  DaemonBootstrap bootstrap;
  assert_true(!bootstrap.run(*context),
              "bind conflict failure injection should fail before daemon reaches ready");
}

void peer_identity_unsupported_returns_fail_closed_rejection() {
  using namespace std::chrono_literals;

  auto ipc = std::make_shared<UnsupportedPeerIpc>();
  int runtime_calls = 0;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.runtime_dispatch_backend = [&runtime_calls](const RuntimeDispatchRequest&) {
    ++runtime_calls;
    return RuntimeDispatchResult{};
  };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr && gateway->init(),
              "unsupported peer failure injection should initialize daemon gateway");

  ScopedTempDirectory temp_root("daemon-peer-unsupported");
  DaemonBootstrapConfig config;
  config.socket_path = (temp_root.path() / "control.sock").string();

  const auto context = DaemonBootstrap::build(
      config,
      DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.failure.peer_identity",
          .config_revision = std::nullopt,
      });
  assert_true(context.has_value(),
              "unsupported peer failure injection should build process context");

  DaemonBootstrap bootstrap;
  bool run_ok = false;
  std::thread daemon_thread([&bootstrap, &context, &run_ok]() {
    run_ok = bootstrap.run(*context);
  });

  std::string response_text;
  const bool received = ipc->wait_for_response(200ms, &response_text);
  bootstrap.stop();
  if (daemon_thread.joinable()) {
    daemon_thread.join();
  }

  assert_true(received,
              "unsupported peer failure injection should capture daemon rejection response");
  const auto decoded = dasall::access::daemon::decode_response_frame(response_text);
  assert_true(decoded.ok(),
              "unsupported peer failure injection should emit a valid daemon response frame");
  assert_true(decoded.frame.error_ref.has_value(),
              "unsupported peer failure injection should surface a stable error_ref");
  assert_equal(std::string("authentication_failed"), *decoded.frame.error_ref,
               "unsupported peer failure injection should fail closed before runtime dispatch");
  assert_equal(0, runtime_calls,
               "unsupported peer failure injection should not enter runtime backend");
  assert_true(run_ok,
              "unsupported peer failure injection should still let daemon loop exit cleanly after stop");
}

void runtime_timeout_failure_is_surfaced_over_daemon_wire() {
  int runtime_calls = 0;

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_profile_id = "daemon.failure.runtime_timeout";
  options.runtime_dispatch_backend = [&runtime_calls](const RuntimeDispatchRequest&) {
    ++runtime_calls;
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Rejected;
    result.error_ref = "runtime_dispatch_timeout";
    return result;
  };

  DaemonIntegrationHarness harness(std::move(options));
  const auto response = harness.make_client().submit("timeout injected");

  assert_true(response.ok(),
              "runtime-timeout failure injection should parse daemon rejection response");
  assert_true(response.error_ref.has_value(),
              "runtime-timeout failure injection should surface timeout error_ref");
  assert_equal(std::string("runtime_dispatch_timeout"), *response.error_ref,
               "runtime-timeout failure injection should preserve timeout failure key over wire");
  assert_equal(1, runtime_calls,
               "runtime-timeout failure injection should still traverse runtime dispatch seam once");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "runtime-timeout failure injection should stop daemon cleanly");
}

}  // namespace

int main() {
  try {
    bind_conflict_returns_failed_startup();
    peer_identity_unsupported_returns_fail_closed_rejection();
    runtime_timeout_failure_is_surfaced_over_daemon_wire();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonFailureInjectionIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}