#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

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

class StubGateway final : public IAccessGateway {
 public:
  explicit StubGateway(bool ready) : ready_(ready) {}

  bool init() override { return ready_; }

  RuntimeDispatchResult submit(const InboundPacket&) override {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Rejected;
    result.error_ref = "unused";
    return result;
  }

  bool publish_result(const PublishEnvelope&) override { return true; }

  [[nodiscard]] AccessGatewayState state() const override {
    return ready_ ? AccessGatewayState::Ready : AccessGatewayState::Uninitialized;
  }

  [[nodiscard]] bool is_ready() const override { return ready_; }

  void shutdown(std::chrono::milliseconds) override {}

 private:
  bool ready_ = false;
};

class CountingIpc final : public dasall::platform::IIPC {
 public:
  int listen_calls = 0;
  int accept_calls = 0;

  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint&,
                                           const ListenOptions&) override {
    ++listen_calls;
    return PlatformResult<IpcListenerHandle>::success(
        IpcListenerHandle{.native_fd = 11U});
  }

  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle&,
                                          std::int32_t) override {
    ++accept_calls;
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "accept should not be reached in DaemonBootstrapTest failure paths"));
  }

  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "connect is not used by DaemonBootstrapTest"));
  }

  PlatformResult<IpcSendResult> send(const IpcChannelHandle&, const IpcPayload&) override {
    return PlatformResult<IpcSendResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "send is not used by DaemonBootstrapTest"));
  }

  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle&, std::int32_t) override {
    return PlatformResult<IpcReceiveResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "receive is not used by DaemonBootstrapTest"));
  }

  PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle&) override {
    return PlatformResult<PeerIdentitySnapshot>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "describe_peer is not used by DaemonBootstrapTest"));
  }

  PlatformResult<bool> close(const IpcChannelHandle&) override {
    return PlatformResult<bool>::success(true);
  }
};

void test_build_returns_process_context_for_valid_dependencies() {
  auto ipc = std::make_shared<CountingIpc>();
  auto gateway = std::make_shared<StubGateway>(true);

  DaemonBootstrapConfig config;
  config.socket_path = "/tmp/daemon-bootstrap-build.sock";

  const auto context = DaemonBootstrap::build(
      config,
      DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.test.profile",
          .config_revision = std::string("rev-009"),
      });

  assert_true(context.has_value(),
              "build should return a process context when config and dependencies are valid");
  assert_true(context->has_consistent_values(),
              "build should return a consistent process context on success");
  assert_equal(std::string("daemon.test.profile"),
               context->effective_profile_id,
               "build should preserve the effective profile id in process context");
  assert_true(context->config_revision.has_value() &&
                  *context->config_revision == "rev-009",
              "build should preserve the config revision in process context");
}

void test_build_failure_does_not_return_half_initialized_context() {
  auto ipc = std::make_shared<CountingIpc>();
  auto ready_gateway = std::make_shared<StubGateway>(true);

  DaemonBootstrapConfig invalid_config;
  invalid_config.socket_path.clear();
  const auto invalid_config_context = DaemonBootstrap::build(
      invalid_config,
      DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = ready_gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.invalid",
          .config_revision = std::nullopt,
      });
  assert_true(!invalid_config_context.has_value(),
              "build should reject invalid config instead of returning a half-initialized context");

  const auto missing_dependency_context = DaemonBootstrap::build(
      DaemonBootstrapConfig{},
      DaemonBootstrap::BuildDependencies{
          .ipc = nullptr,
          .access_gateway = ready_gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.missing-deps",
          .config_revision = std::nullopt,
      });
  assert_true(!missing_dependency_context.has_value(),
              "build should reject missing dependency bundles instead of returning a partial context");
}

void test_run_does_not_bind_or_accept_when_gateway_is_not_ready() {
  auto ipc = std::make_shared<CountingIpc>();
  auto gateway = std::make_shared<StubGateway>(false);

  DaemonProcessContext context;
  context.bootstrap_config.socket_path = "/tmp/daemon-bootstrap-not-ready.sock";
  context.effective_profile_id = "daemon.not-ready";
  context.ipc = ipc;
  context.access_gateway = gateway;
  context.watchdog_service = nullptr;
  context.config_revision = std::nullopt;

  DaemonBootstrap bootstrap;
  const bool run_ok = bootstrap.run(context);
  assert_true(!run_ok,
              "run(context) should reject contexts whose access gateway is not ready");
  assert_equal(0, ipc->listen_calls,
               "run(context) should not bind the listener when gateway readiness is false");
  assert_equal(0, ipc->accept_calls,
               "run(context) should not accept requests when gateway readiness is false");
}

}  // namespace

int main() {
  try {
    test_build_returns_process_context_for_valid_dependencies();
    test_build_failure_does_not_return_half_initialized_context();
    test_run_does_not_bind_or_accept_when_gateway_is_not_ready();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonBootstrapTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}