#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "DaemonListenerHost.h"
#include "PlatformError.h"
#include "support/TestAssertions.h"

namespace {

using dasall::apps::daemon::DaemonListenerHost;
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

class ScriptedIpc final : public dasall::platform::IIPC {
 public:
  PlatformResult<IpcListenerHandle> listen_result =
      PlatformResult<IpcListenerHandle>::success(IpcListenerHandle{.native_fd = 41U});
  std::vector<PlatformResult<IpcChannelHandle>> accept_results;
  PlatformResult<bool> close_result = PlatformResult<bool>::success(true);

  std::optional<IpcEndpoint> last_endpoint;
  std::optional<ListenOptions> last_listen_options;
  std::vector<std::int32_t> accept_deadlines;
  std::vector<std::uint64_t> closed_channels;

  PlatformResult<IpcListenerHandle> listen(const IpcEndpoint& endpoint,
                                           const ListenOptions& options) override {
    last_endpoint = endpoint;
    last_listen_options = options;
    return listen_result;
  }

  PlatformResult<IpcChannelHandle> accept(const IpcListenerHandle&, std::int32_t deadline_ms) override {
    accept_deadlines.push_back(deadline_ms);
    if (!accept_results.empty()) {
      const auto result = accept_results.front();
      accept_results.erase(accept_results.begin());
      return result;
    }

    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::Timeout,
        PlatformErrorCategory::IPC,
        "scripted accept timeout"));
  }

  PlatformResult<IpcChannelHandle> connect(const IpcEndpoint&, std::int32_t) override {
    return PlatformResult<IpcChannelHandle>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "connect is not used by DaemonListenerHostTest"));
  }

  PlatformResult<IpcSendResult> send(const IpcChannelHandle&, const IpcPayload&) override {
    return PlatformResult<IpcSendResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "send is not used by DaemonListenerHostTest"));
  }

  PlatformResult<IpcReceiveResult> receive(const IpcChannelHandle&, std::int32_t) override {
    return PlatformResult<IpcReceiveResult>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "receive is not used by DaemonListenerHostTest"));
  }

  PlatformResult<PeerIdentitySnapshot> describe_peer(const IpcChannelHandle&) override {
    return PlatformResult<PeerIdentitySnapshot>::failure(make_error(
        PlatformErrorCode::InternalFailure,
        PlatformErrorCategory::Internal,
        "describe_peer is not used by DaemonListenerHostTest"));
  }

  PlatformResult<bool> close(const IpcChannelHandle& handle) override {
    closed_channels.push_back(handle.native_fd);
    return close_result;
  }
};

void test_bind_passes_expected_direct_bind_options() {
  auto ipc = std::make_shared<ScriptedIpc>();
  DaemonListenerHost host(ipc);

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/daemon-listener.sock";

  const auto bind_result = host.bind(endpoint);
  assert_true(bind_result.ok(), "bind should succeed for valid endpoint");
  assert_true(host.is_bound(), "listener host should report bound after successful bind");
  assert_true(ipc->last_endpoint.has_value(), "bind should pass endpoint to ipc.listen");
  assert_equal(endpoint.socket_path,
               ipc->last_endpoint->socket_path,
               "bind should forward socket path to ipc.listen");
  assert_true(ipc->last_listen_options.has_value(),
              "bind should pass listen options to ipc.listen");
  assert_equal(static_cast<std::uint32_t>(8U),
               ipc->last_listen_options->backlog,
               "bind should use daemon direct-bind backlog");
  assert_equal(static_cast<std::uint32_t>(1048576U),
               ipc->last_listen_options->max_payload_bytes,
               "bind should use daemon max payload budget");
}

void test_accept_loop_tolerates_timeout_dispatches_connection_and_closes_channel() {
  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->accept_results.push_back(PlatformResult<IpcChannelHandle>::failure(make_error(
      PlatformErrorCode::Timeout,
      PlatformErrorCategory::IPC,
      "accept timed out while polling stop flag")));
  ipc->accept_results.push_back(
      PlatformResult<IpcChannelHandle>::success(IpcChannelHandle{.native_fd = 77U}));

  DaemonListenerHost host(ipc);
  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/daemon-listener-timeout.sock";
  const auto bind_result = host.bind(endpoint);
  assert_true(bind_result.ok(), "bind should succeed before accept loop test");

  std::atomic<bool> stop_requested{false};
  int handled_connections = 0;
  host.set_connection_handler([&stop_requested, &handled_connections](const IpcChannelHandle& channel) {
    ++handled_connections;
    stop_requested.store(true);
    return channel.has_consistent_values();
  });

  const auto loop_result = host.accept_loop(stop_requested, 25);
  assert_true(loop_result.ok(), "accept loop should survive timeout and dispatch next connection");
  assert_equal(1, handled_connections,
               "accept loop should invoke handler exactly once for scripted connection");
  assert_equal(static_cast<std::size_t>(1U),
               ipc->closed_channels.size(),
               "accept loop should close handled channel");
  assert_equal(static_cast<std::uint64_t>(77U),
               ipc->closed_channels.front(),
               "accept loop should close the accepted channel handle");
  assert_equal(static_cast<std::size_t>(2U),
               ipc->accept_deadlines.size(),
               "accept loop should keep polling after timeout until a connection arrives");
}

void test_close_rejects_future_accept_loops() {
  auto ipc = std::make_shared<ScriptedIpc>();
  DaemonListenerHost host(ipc);

  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/daemon-listener-close.sock";
  const auto bind_result = host.bind(endpoint);
  assert_true(bind_result.ok(), "bind should succeed before close test");

  const auto close_result = host.close();
  assert_true(close_result.ok(), "close should succeed for bound listener host");
  assert_true(!host.is_bound(), "listener host should report unbound after close");

  std::atomic<bool> stop_requested{false};
  host.set_connection_handler([](const IpcChannelHandle&) { return true; });
  const auto loop_result = host.accept_loop(stop_requested, 10);
  assert_true(!loop_result.ok(), "accept loop should reject calls after listener host close");
  assert_equal(static_cast<int>(PlatformErrorCode::NotFound),
               static_cast<int>(loop_result.error->code),
               "accept loop after close should map to NotFound");
}

void test_accept_loop_maps_listener_errors() {
  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->accept_results.push_back(PlatformResult<IpcChannelHandle>::failure(make_error(
      PlatformErrorCode::PermissionDenied,
      PlatformErrorCategory::IPC,
      "listener accept permission denied")));

  DaemonListenerHost host(ipc);
  IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/daemon-listener-error.sock";
  const auto bind_result = host.bind(endpoint);
  assert_true(bind_result.ok(), "bind should succeed before listener error mapping test");

  std::atomic<bool> stop_requested{false};
  host.set_connection_handler([](const IpcChannelHandle&) { return true; });
  const auto loop_result = host.accept_loop(stop_requested, 15);
  assert_true(!loop_result.ok(), "accept loop should surface non-timeout listener errors");
  assert_equal(static_cast<int>(PlatformErrorCode::PermissionDenied),
               static_cast<int>(loop_result.error->code),
               "accept loop should preserve platform error code from ipc.accept");
}

}  // namespace

int main() {
  try {
    test_bind_passes_expected_direct_bind_options();
    test_accept_loop_tolerates_timeout_dispatches_connection_and_closes_channel();
    test_close_rejects_future_accept_loops();
    test_accept_loop_maps_listener_errors();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonListenerHostTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}