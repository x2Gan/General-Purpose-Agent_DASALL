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
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSubmitTurnRequest;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::ipc::TuiIpcController;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcResponseEnvelope;
using dasall::tui::ipc::TuiIpcSubmitTurnPayload;

dasall::platform::PlatformError make_platform_error(
    const dasall::platform::PlatformErrorCode code,
    const dasall::platform::PlatformErrorCategory category,
    std::string detail,
    const bool retryable = false,
    std::string syscall_name = {},
    std::optional<int> errno_value = std::nullopt) {
  return dasall::platform::PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = retryable,
      .syscall_name = std::move(syscall_name),
      .errno_value = errno_value,
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

class ScriptedIpc final : public dasall::platform::IIPC {
 public:
  std::optional<dasall::platform::PlatformError> connect_error;
  std::optional<dasall::platform::PlatformError> send_error;
  std::optional<dasall::platform::PlatformError> receive_error;
  std::string response_text;
  std::string last_sent_payload;
  std::int32_t last_connect_deadline_ms = -1;
  std::int32_t last_receive_deadline_ms = -1;
  bool peer_closed = false;
  bool close_called = false;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "listen unused in tui ipc controller unit tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "accept unused in tui ipc controller unit tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      const std::int32_t deadline_ms) override {
    last_connect_deadline_ms = deadline_ms;
    if (connect_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcChannelHandle>::failure(*connect_error);
    }

    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 71U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    if (send_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcSendResult>::failure(*send_error);
    }

    last_sent_payload.assign(reinterpret_cast<const char*>(payload.data()),
                             payload.size());
    return dasall::platform::PlatformResult<
        dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      const std::int32_t deadline_ms) override {
    last_receive_deadline_ms = deadline_ms;
    if (receive_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcReceiveResult>::failure(*receive_error);
    }

    dasall::platform::IpcReceiveResult result;
    result.data = make_payload(response_text);
    result.peer_closed = peer_closed;
    return dasall::platform::PlatformResult<
        dasall::platform::IpcReceiveResult>::success(std::move(result));
  }

  dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::PeerIdentitySnapshot>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "describe_peer unused in tui ipc controller unit tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    close_called = true;
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
  options.socket_path = "/tmp/dasall-tui-ipc-controller-test.sock";
  return options;
}

TuiSubmitTurnRequest make_submit_request() {
  NextTurnPreference preference;
  preference.mode = TuiRoutePreferenceMode::PinModel;
  preference.pinned_provider_id = std::string("deepseek-prod");
  preference.pinned_model_id = std::string("deepseek-chat");
  preference.user_visible_summary = "pin next turn to deepseek chat";
  preference.source = "unit_test";

  return TuiSubmitTurnRequest{
      .session_id = "session-022",
      .user_input = "Summarize the current route state",
      .next_preference = std::move(preference),
      .request_id = "req-submit-022",
      .trace_id = "trace-submit-022",
  };
}

TuiOpenSessionRequest make_open_request() {
  return TuiOpenSessionRequest{
      .profile_id = std::string("desktop_full"),
      .startup_mode_hint = std::string("full"),
      .request_id = "req-open-022",
      .trace_id = "trace-open-022",
  };
}

void submit_turn_roundtrip_preserves_request_context_and_receipt() {
  auto ipc = std::make_shared<ScriptedIpc>();
  const auto request = make_submit_request();

  TuiTurnReceipt receipt;
  receipt.request_id = request.request_id;
  receipt.trace_id = request.trace_id;
  receipt.session_id = request.session_id;
  receipt.disposition = "accepted_async";
  receipt.receipt_ref = "receipt-022";
  receipt.submitted_at = "2026-05-23T13:22:00Z";
  receipt.summary_text = "queued for daemon execution";

  TuiIpcResponseEnvelope response;
  response.operation = TuiIpcOperation::SubmitTurn;
  response.request_id = request.request_id;
  response.trace_id = request.trace_id;
  response.session_id = request.session_id;
  response.outcome = TuiIpcOutcome::Success;
  response.payload = receipt;
  ipc->response_text =
      dasall::tui::ipc::test::encode_response_envelope_for_test(response);

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  TuiIpcController controller(make_options());
  const auto result = controller.submit_turn(request);

  assert_true(result.ok() && result.has_consistent_values(),
              "submit_turn should return a stable receipt on successful roundtrip");
  assert_equal(std::string("receipt-022"),
               result.receipt->receipt_ref,
               "submit_turn should preserve receipt_ref from daemon response");
  assert_true(ipc->close_called,
              "tui ipc controller should close the channel after a roundtrip");
  assert_true(ipc->last_connect_deadline_ms == 15000,
              "submit_turn should use the submit deadline for connect");
  assert_true(ipc->last_receive_deadline_ms == 15000,
              "submit_turn should use the submit deadline for receive");

  const auto encoded_request =
      dasall::tui::ipc::test::decode_request_envelope_for_test(
          ipc->last_sent_payload);
  assert_true(encoded_request.has_value(),
              "submit_turn should emit a parseable tui request envelope");
  assert_true(encoded_request->operation == TuiIpcOperation::SubmitTurn,
              "submit_turn should encode the submit_turn operation");
  assert_equal(std::string("session-022"),
               encoded_request->session_id.value_or(std::string()),
               "submit_turn should encode caller session_id into the envelope");
  const auto* payload =
      std::get_if<TuiIpcSubmitTurnPayload>(&encoded_request->payload);
  assert_true(payload != nullptr,
              "submit_turn should encode the submit_turn payload variant");
  assert_equal(std::string("deepseek-prod"),
               payload->next_preference.pinned_provider_id.value_or(std::string()),
               "submit_turn should preserve next-turn pinned provider in the wire payload");
}

void socket_missing_is_normalized_from_connect_failures() {
  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->connect_error = make_platform_error(
      dasall::platform::PlatformErrorCode::NotFound,
      dasall::platform::PlatformErrorCategory::IPC,
      "connect() failed for socket path '/tmp/dasall-tui-ipc-controller-test.sock': No such file or directory",
      false,
      "connect",
      2);

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  TuiIpcController controller(make_options());
  const auto result = controller.open_session(make_open_request());

  assert_true(!result.ok() && result.has_consistent_values(),
              "open_session should surface socket-missing failures as an issue");
  assert_equal(std::string("transport"),
               result.issue->reason_domain,
               "socket-missing failures should stay in the transport domain");
  assert_equal(std::string("socket_missing"),
               result.issue->reason_code,
               "socket-missing failures should expose a stable reason code");
  assert_true(find_metadata_value(result.issue->metadata, "socket_path").has_value(),
              "socket-missing failures should keep socket_path metadata for diagnostics");
}

void timeout_is_normalized_from_receive_failures() {
  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->receive_error = make_platform_error(
      dasall::platform::PlatformErrorCode::Timeout,
      dasall::platform::PlatformErrorCategory::IPC,
      "ipc receive timed out before payload arrival",
      true,
      "recv");

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  TuiIpcController controller(make_options());
  const auto result = controller.open_session(make_open_request());

  assert_true(!result.ok() && result.has_consistent_values(),
              "receive timeouts should surface as a consistent open_session issue");
  assert_equal(std::string("transport"),
               result.issue->reason_domain,
               "receive timeouts should stay in the transport domain");
  assert_equal(std::string("timeout"),
               result.issue->reason_code,
               "receive timeouts should expose the stable timeout reason code");
  assert_true(result.issue->retryable,
              "receive timeouts should remain retryable for later startup recovery");
}

void schema_mismatch_and_malformed_response_stay_distinct() {
  {
    auto ipc = std::make_shared<ScriptedIpc>();
    const auto request = make_open_request();

    dasall::tui::data::TuiSessionView session;
    session.session_id = "session-schema";
    session.profile_id = "desktop_full";
    session.daemon_readiness = "ready";
    session.startup_mode = "full";
    session.started_at = "2026-05-23T13:24:00Z";

    TuiIpcResponseEnvelope response;
    response.schema_version = "tui_ipc.v2";
    response.operation = TuiIpcOperation::OpenSession;
    response.request_id = request.request_id;
    response.trace_id = request.trace_id;
    response.session_id = std::string("session-schema");
    response.outcome = TuiIpcOutcome::Success;
    response.payload = session;
    ipc->response_text =
        dasall::tui::ipc::test::encode_response_envelope_for_test(response);

    const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
    TuiIpcController controller(make_options());
    const auto result = controller.open_session(request);

    assert_true(!result.ok() && result.has_consistent_values(),
                "schema mismatch should surface as a consistent issue");
    assert_equal(std::string("protocol"),
                 result.issue->reason_domain,
                 "schema mismatch should stay in the protocol domain");
    assert_equal(std::string("schema_mismatch"),
                 result.issue->reason_code,
                 "schema mismatch should not collapse into malformed_response");
    assert_equal(std::string("tui_ipc.v2"),
                 find_metadata_value(result.issue->metadata,
                                     "actual_schema_version")
                     .value_or(std::string()),
                 "schema mismatch should preserve the incompatible schema version");
  }

  {
    auto ipc = std::make_shared<ScriptedIpc>();
    ipc->response_text =
        "{\"schema_version\":\"tui_ipc.v1\",\"operation\":\"open_session\",\"request_id\":\"req-open-022\",\"trace_id\":\"trace-open-022\",\"outcome\":\"success\"}";

    const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
    TuiIpcController controller(make_options());
    const auto result = controller.open_session(make_open_request());

    assert_true(!result.ok() && result.has_consistent_values(),
                "missing payload should surface as a consistent malformed-response issue");
    assert_equal(std::string("protocol"),
                 result.issue->reason_domain,
                 "missing payload should stay in the protocol domain");
    assert_equal(std::string("malformed_response"),
                 result.issue->reason_code,
                 "missing payload should stay distinct from schema mismatch");
  }
}

}  // namespace

int main() {
  try {
    submit_turn_roundtrip_preserves_request_context_and_receipt();
    socket_missing_is_normalized_from_connect_failures();
    timeout_is_normalized_from_receive_failures();
    schema_mismatch_and_malformed_response_stay_distinct();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}