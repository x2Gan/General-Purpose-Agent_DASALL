#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "PlatformResult.h"
#include "daemon/TuiIpcProtocolAdapter.h"
#include "support/TestAssertions.h"

namespace {

class CapturingIpc final : public dasall::platform::IIPC {
 public:
  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle>::success(
        dasall::platform::IpcListenerHandle{.native_fd = 11U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 12U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 13U});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    sent_payload = payload;
    return dasall::platform::PlatformResult<dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult>::success(
        dasall::platform::IpcReceiveResult{});
  }

  [[nodiscard]] dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>::success(
        dasall::platform::PeerIdentitySnapshot{
            .peer_uid = 1000U,
            .peer_gid = 1000U,
            .peer_pid = 4242U,
            .is_local_socket_peer = true,
        });
  }

  [[nodiscard]] dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }

  dasall::platform::IpcPayload sent_payload;
};

class FakeAccessGateway final : public dasall::access::IAccessGateway {
 public:
  bool init() override {
    return true;
  }

  dasall::access::RuntimeDispatchResult submit(
      const dasall::access::InboundPacket& packet) override {
    submit_count += 1U;
    last_packet = packet;
    return next_result;
  }

  bool publish_result(const dasall::access::PublishEnvelope&) override {
    return true;
  }

  [[nodiscard]] dasall::access::AccessGatewayState state() const override {
    return dasall::access::AccessGatewayState::Ready;
  }

  [[nodiscard]] bool is_ready() const override {
    return true;
  }

  void shutdown(std::chrono::milliseconds) override {}

  std::size_t submit_count = 0U;
  dasall::access::InboundPacket last_packet;
  dasall::access::RuntimeDispatchResult next_result;
};

[[nodiscard]] std::vector<std::uint8_t> make_payload(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::string_view as_text(const dasall::platform::IpcPayload& payload) {
  return std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size());
}

[[nodiscard]] std::string build_request(std::string_view operation,
                                        std::string_view request_id,
                                        std::string_view trace_id,
                                        std::string_view payload_json,
                                        std::string_view session_id = {}) {
  std::string json =
      std::string("{\"schema_version\":\"tui_ipc.v1\",\"operation\":\"") +
      std::string(operation) +
      "\",\"request_id\":\"" + std::string(request_id) +
      "\",\"trace_id\":\"" + std::string(trace_id) + "\",";
  if (!session_id.empty()) {
    json += "\"session_id\":\"" + std::string(session_id) + "\",";
  }
  json += "\"deadline_ms\":3000,\"payload\":" + std::string(payload_json) + "}";
  return json;
}

void assert_contains(std::string_view text,
                     std::string_view needle,
                     std::string_view message) {
  dasall::tests::support::assert_true(text.find(needle) != std::string_view::npos,
                                      std::string(message));
}

void tui_ipc_protocol_adapter_decodes_open_session_and_encodes_success_response() {
  using dasall::access::daemon::TuiIpcOperation;
  using dasall::access::daemon::TuiIpcOutcome;
  using dasall::access::daemon::TuiIpcProtocolAdapter;
  using dasall::access::daemon::TuiIpcSessionStore;
  using dasall::access::daemon::TuiIpcSessionView;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<CapturingIpc>();
  TuiIpcProtocolAdapter adapter(ipc);
  dasall::platform::IpcChannelHandle channel{.native_fd = 7U};
  adapter.set_active_channel(
      channel,
      make_payload(build_request(
          "open_session",
          "req-open-038",
          "trace-open-038",
          R"({"profile_id":"desktop_full","startup_mode_hint":"full"})")));

  assert_true(adapter.payload_looks_like_tui_ipc(),
              "open_session payload should be recognized as tui_ipc.v1");

  const auto decoded = adapter.decode_tui_ipc_request();
  assert_true(decoded.ok(), "open_session request should decode successfully");
  assert_equal(static_cast<int>(TuiIpcOperation::OpenSession),
               static_cast<int>(decoded.envelope->operation),
               "decoded request should preserve open_session operation");

  FakeAccessGateway gateway;
  TuiIpcSessionStore session_store;
  const auto response = adapter.dispatch_tui_ipc_operation(
      decoded, gateway, session_store, "local_trusted:1000", "desktop_full");

  assert_true(response.outcome == TuiIpcOutcome::Success,
              "open_session should produce a success envelope");
  const auto* session = std::get_if<TuiIpcSessionView>(&*response.payload);
  assert_true(session != nullptr,
              "open_session should return a session payload projection");
  assert_equal(std::string("desktop_full"),
               session->profile_id,
               "open_session should preserve requested profile id in the response projection");

  assert_true(adapter.encode_tui_ipc_response(response),
              "success envelope should encode back onto the active IPC channel");
  const auto encoded = as_text(ipc->sent_payload);
  assert_contains(encoded,
                  R"("schema_version":"tui_ipc.v1")",
                  "encoded response should preserve the frozen schema version");
  assert_contains(encoded,
                  R"("operation":"open_session")",
                  "encoded response should preserve the open_session operation");
  assert_contains(encoded,
                  R"("outcome":"success")",
                  "encoded response should serialize a success outcome");
}

void tui_ipc_protocol_adapter_surfaces_schema_mismatch_unknown_operation_and_validation_rejected() {
  using dasall::access::daemon::TuiIpcDecodeError;
  using dasall::access::daemon::TuiIpcProtocolAdapter;
  using dasall::access::daemon::TuiIpcSessionStore;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<CapturingIpc>();
  FakeAccessGateway gateway;
  TuiIpcSessionStore session_store;
  dasall::platform::IpcChannelHandle channel{.native_fd = 9U};

  {
    TuiIpcProtocolAdapter adapter(ipc);
    adapter.set_active_channel(
        channel,
        make_payload(
            R"({"schema_version":"tui_ipc.v0","operation":"open_session","request_id":"req-schema-038","trace_id":"trace-schema-038","deadline_ms":3000,"payload":{}})"));
    const auto decoded = adapter.decode_tui_ipc_request();
    assert_equal(static_cast<int>(TuiIpcDecodeError::SchemaMismatch),
                 static_cast<int>(decoded.error),
                 "schema mismatch should decode into the stable schema_mismatch error");
    const auto response = adapter.dispatch_tui_ipc_operation(
        decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
    assert_true(adapter.encode_tui_ipc_response(response),
                "schema mismatch failure should still encode a tui response envelope");
    assert_contains(as_text(ipc->sent_payload),
                    R"("reason_code":"schema_mismatch")",
                    "schema mismatch should remain machine-readable on the wire");
  }

  {
    TuiIpcProtocolAdapter adapter(ipc);
    adapter.set_active_channel(
        channel,
        make_payload(
            R"({"schema_version":"tui_ipc.v1","operation":"explode_session","request_id":"req-unknown-038","trace_id":"trace-unknown-038","deadline_ms":3000,"payload":{}})"));
    const auto decoded = adapter.decode_tui_ipc_request();
    assert_equal(static_cast<int>(TuiIpcDecodeError::UnknownOperation),
                 static_cast<int>(decoded.error),
                 "unknown operations should decode into the stable unknown_operation error");
    const auto response = adapter.dispatch_tui_ipc_operation(
        decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
    assert_true(adapter.encode_tui_ipc_response(response),
                "unknown operation failure should still encode a tui response envelope");
    assert_contains(as_text(ipc->sent_payload),
                    R"("reason_code":"unknown_operation")",
                    "unknown operations should remain machine-readable on the wire");
  }

  {
    TuiIpcProtocolAdapter adapter(ipc);
    adapter.set_active_channel(
        channel,
        make_payload(build_request(
            "submit_turn",
            "req-validate-038",
            "trace-validate-038",
            R"({"user_input":"","next_preference":{"mode":"auto","user_visible_summary":"auto","source":"daemon","applies_to_next_turn_only":true}})")));
    const auto decoded = adapter.decode_tui_ipc_request();
    assert_equal(static_cast<int>(TuiIpcDecodeError::ValidationRejected),
                 static_cast<int>(decoded.error),
                 "validation failures should decode into the stable validation_failed error");
    const auto response = adapter.dispatch_tui_ipc_operation(
        decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
    assert_true(adapter.encode_tui_ipc_response(response),
                "validation failure should still encode a tui response envelope");
    assert_contains(as_text(ipc->sent_payload),
                    R"("reason_code":"validation_failed")",
                    "validation failures should remain machine-readable on the wire");
  }
}

void daemon_tui_ipc_server_handler_dispatches_submit_poll_and_close() {
  using dasall::access::AccessDisposition;
  using dasall::access::daemon::TuiIpcCloseSessionAck;
  using dasall::access::daemon::TuiIpcPollEventsBatch;
  using dasall::access::daemon::TuiIpcProtocolAdapter;
  using dasall::access::daemon::TuiIpcSessionStore;
  using dasall::access::daemon::TuiIpcSessionView;
  using dasall::access::daemon::TuiIpcTurnReceipt;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto ipc = std::make_shared<CapturingIpc>();
  FakeAccessGateway gateway;
  gateway.next_result.disposition = AccessDisposition::AcceptedAsync;
  gateway.next_result.receipt_ref = std::string("receipt-038");
  TuiIpcSessionStore session_store;
  dasall::platform::IpcChannelHandle channel{.native_fd = 17U};

  TuiIpcProtocolAdapter open_adapter(ipc);
  open_adapter.set_active_channel(
      channel,
      make_payload(build_request(
          "open_session",
          "req-open-038b",
          "trace-open-038b",
          R"({"profile_id":"desktop_full","startup_mode_hint":"full"})")));
  const auto open_decoded = open_adapter.decode_tui_ipc_request();
  const auto open_response = open_adapter.dispatch_tui_ipc_operation(
      open_decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
  const auto* session = std::get_if<TuiIpcSessionView>(&*open_response.payload);
  assert_true(session != nullptr,
              "open_session should produce a session payload before submit_turn");

  TuiIpcProtocolAdapter submit_adapter(ipc);
  submit_adapter.set_active_channel(
      channel,
      make_payload(build_request(
          "submit_turn",
          "req-submit-038",
          "trace-submit-038",
          R"({"user_input":"hello daemon","next_preference":{"mode":"prefer_depth","preferred_depth_tier":"deep","user_visible_summary":"prefer deep","source":"selector","applies_to_next_turn_only":true}})",
          session->session_id)));
  const auto submit_decoded = submit_adapter.decode_tui_ipc_request();
  const auto submit_response = submit_adapter.dispatch_tui_ipc_operation(
      submit_decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
  const auto* receipt = std::get_if<TuiIpcTurnReceipt>(&*submit_response.payload);
  assert_true(receipt != nullptr,
              "submit_turn should project a turn receipt on accepted_async dispatch");
  assert_equal(std::string("accepted_async"),
               receipt->disposition,
               "submit_turn should preserve accepted_async disposition through the server handler");
  assert_equal(std::string("receipt-038"),
               receipt->receipt_ref,
               "submit_turn should preserve the access gateway receipt ref");
  assert_equal(std::string("hello daemon"),
               gateway.last_packet.payload,
               "submit_turn should forward user_input into the access gateway packet payload");
  assert_equal(std::string("tui_ipc.v1"),
               gateway.last_packet.protocol_kind,
               "submit_turn should freeze protocol_kind to tui_ipc.v1 on the access gateway packet");
  assert_equal(std::string("deep"),
               gateway.last_packet.headers.at("tui_preferred_depth_tier"),
               "submit_turn should preserve next-turn preference fields through the access packet headers");

  TuiIpcProtocolAdapter poll_adapter(ipc);
  poll_adapter.set_active_channel(
      channel,
      make_payload(build_request(
          "poll_events",
          "req-poll-038",
          "trace-poll-038",
          R"({})",
          session->session_id)));
  const auto poll_decoded = poll_adapter.decode_tui_ipc_request();
  const auto poll_response = poll_adapter.dispatch_tui_ipc_operation(
      poll_decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
  const auto* batch = std::get_if<TuiIpcPollEventsBatch>(&*poll_response.payload);
  assert_true(batch != nullptr,
              "poll_events should project an event batch after submit_turn");
  assert_equal(1, static_cast<int>(batch->events.size()),
               "poll_events should return the queued submit receipt event exactly once");
  assert_true(batch->events.front().turn_receipt.has_value(),
              "poll_events should surface the submit receipt inside the event batch projection");

  TuiIpcProtocolAdapter close_adapter(ipc);
  close_adapter.set_active_channel(
      channel,
      make_payload(build_request(
          "close_session",
          "req-close-038",
          "trace-close-038",
          R"({"close_reason":"/exit"})",
          session->session_id)));
  const auto close_decoded = close_adapter.decode_tui_ipc_request();
  const auto close_response = close_adapter.dispatch_tui_ipc_operation(
      close_decoded, gateway, session_store, "local_trusted:1000", "desktop_full");
  const auto* close_ack = std::get_if<TuiIpcCloseSessionAck>(&*close_response.payload);
  assert_true(close_ack != nullptr && close_ack->closed,
              "close_session should acknowledge successful session closure");
}

}  // namespace

int main() {
  try {
    tui_ipc_protocol_adapter_decodes_open_session_and_encodes_success_response();
    tui_ipc_protocol_adapter_surfaces_schema_mismatch_unknown_operation_and_validation_rejected();
    daemon_tui_ipc_server_handler_dispatches_submit_poll_and_close();
  } catch (const std::exception& ex) {
    std::cerr << "[TuiIpcProtocolAdapterTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}