#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "PlatformError.h"
#include "ipc/TuiIpcController.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::ipc::TuiIpcController;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcResponseEnvelope;

dasall::platform::PlatformError make_platform_error(
    std::string detail) {
  return dasall::platform::PlatformError{
      .code = dasall::platform::PlatformErrorCode::PermissionDenied,
      .category = dasall::platform::PlatformErrorCategory::IPC,
      .retryable_hint = false,
      .syscall_name = "connect",
      .errno_value = 13,
      .detail = std::move(detail),
  };
}

dasall::platform::IpcPayload make_payload(std::string_view text) {
  dasall::platform::IpcPayload payload;
  payload.reserve(text.size());
  for (const char ch : text) {
    payload.push_back(static_cast<std::uint8_t>(ch));
  }
  return payload;
}

class PermissionIpc final : public dasall::platform::IIPC {
 public:
  std::optional<dasall::platform::PlatformError> connect_error;
  std::string response_text;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(make_platform_error(
        "listen unused in tui ipc permission tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(make_platform_error(
        "accept unused in tui ipc permission tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    if (connect_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcChannelHandle>::failure(*connect_error);
    }

    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 93U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = 1U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    dasall::platform::IpcReceiveResult result;
    result.data = make_payload(response_text);
    return dasall::platform::PlatformResult<
        dasall::platform::IpcReceiveResult>::success(std::move(result));
  }

  dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::PeerIdentitySnapshot>::failure(make_platform_error(
        "describe_peer unused in tui ipc permission tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

std::optional<std::string> find_metadata_value(
    const std::vector<std::pair<std::string, std::string>>& metadata,
    std::string_view key) {
  for (const auto& [entry_key, entry_value] : metadata) {
    if (entry_key == key) {
      return entry_value;
    }
  }
  return std::nullopt;
}

TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-ipc-permission.sock";
  return options;
}

TuiOpenSessionRequest make_request() {
  return TuiOpenSessionRequest{
      .profile_id = std::string("desktop_full"),
      .startup_mode_hint = std::string("full"),
      .request_id = "req-open-perm",
      .trace_id = "trace-open-perm",
  };
}

void connect_permission_denied_is_not_collapsed_into_socket_missing() {
  auto ipc = std::make_shared<PermissionIpc>();
  ipc->connect_error = make_platform_error(
      "connect() failed for socket path '/tmp/dasall-tui-ipc-permission.sock': Permission denied");

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  TuiIpcController controller(make_options());
  const auto result = controller.open_session(make_request());

  assert_true(!result.ok() && result.has_consistent_values(),
              "permission denied should surface as a consistent open_session issue");
  assert_equal(std::string("transport"),
               result.issue->reason_domain,
               "permission denied should stay in the transport domain");
  assert_equal(std::string("permission_denied"),
               result.issue->reason_code,
               "permission denied should not collapse into socket_missing");
  assert_true(!result.issue->retryable,
              "permission denied should stay non-retryable until ownership changes");
  assert_equal(std::string("connect"),
               find_metadata_value(result.issue->metadata, "syscall")
                   .value_or(std::string()),
               "permission denied should preserve syscall metadata");
}

void daemon_failure_permission_denied_is_preserved_verbatim() {
  auto ipc = std::make_shared<PermissionIpc>();

  TuiIpcResponseEnvelope response;
  response.operation = TuiIpcOperation::OpenSession;
  response.request_id = "req-open-perm";
  response.trace_id = "trace-open-perm";
  response.outcome = TuiIpcOutcome::Failure;
  response.reason_domain = std::string("transport");
  response.reason_code = std::string("permission_denied");
  response.message =
      std::string("daemon denied access to the trusted local control socket");
  response.retryable = false;
  response.error_ref = std::string("perm-022");
  response.metadata.emplace_back("socket_path",
                                 "/tmp/dasall-tui-ipc-permission.sock");
  ipc->response_text =
      dasall::tui::ipc::test::encode_response_envelope_for_test(response);

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  TuiIpcController controller(make_options());
  const auto result = controller.open_session(make_request());

  assert_true(!result.ok() && result.has_consistent_values(),
              "permission-denied failure envelopes should surface as a consistent issue");
  assert_equal(std::string("transport"),
               result.issue->reason_domain,
               "permission-denied failure envelopes should preserve their reason domain");
  assert_equal(std::string("permission_denied"),
               result.issue->reason_code,
               "permission-denied failure envelopes should preserve their reason code");
  assert_equal(std::string("perm-022"),
               result.issue->error_ref.value_or(std::string()),
               "permission-denied failure envelopes should preserve error_ref");
}

}  // namespace

int main() {
  try {
    connect_permission_denied_is_not_collapsed_into_socket_missing();
    daemon_failure_permission_denied_is_preserved_verbatim();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}