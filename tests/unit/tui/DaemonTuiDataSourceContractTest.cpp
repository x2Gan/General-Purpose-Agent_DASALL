#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PlatformError.h"
#include "data/DaemonTuiDataSource.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_DAEMON_DATA_SOURCE_HEADER
#define DASALL_TUI_DAEMON_DATA_SOURCE_HEADER \
  "/home/gangan/DASALL/apps/tui/src/data/DaemonTuiDataSource.h"
#endif

#ifndef DASALL_TUI_DAEMON_DATA_SOURCE_IMPL
#define DASALL_TUI_DAEMON_DATA_SOURCE_IMPL \
  "/home/gangan/DASALL/apps/tui/src/data/DaemonTuiDataSource.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiCloseSessionRequest;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiModelRouteProjection;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::data::TuiPollEventsRequest;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogRequest;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSessionView;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::data::TuiSubmitTurnRequest;
using dasall::tui::data::TuiToolSummaryView;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::ipc::TuiIpcCloseSessionAck;
using dasall::tui::ipc::TuiIpcCloseSessionPayload;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOpenSessionPayload;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcResponseEnvelope;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

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
  std::vector<std::string> response_texts;
  std::vector<std::string> sent_payloads;
  std::vector<std::int32_t> connect_deadlines;
  std::vector<std::int32_t> receive_deadlines;
  std::size_t receive_index = 0;
  std::size_t close_count = 0;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "listen unused in daemon data source contract tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "accept unused in daemon data source contract tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      const std::int32_t deadline_ms) override {
    connect_deadlines.push_back(deadline_ms);
    if (connect_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcChannelHandle>::failure(*connect_error);
    }

    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 123U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    if (send_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcSendResult>::failure(*send_error);
    }

    sent_payloads.emplace_back(reinterpret_cast<const char*>(payload.data()),
                               payload.size());
    return dasall::platform::PlatformResult<
        dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      const std::int32_t deadline_ms) override {
    receive_deadlines.push_back(deadline_ms);
    if (receive_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcReceiveResult>::failure(*receive_error);
    }

    dasall::platform::IpcReceiveResult result;
    if (receive_index < response_texts.size()) {
      result.data = make_payload(response_texts.at(receive_index++));
    }
    return dasall::platform::PlatformResult<
        dasall::platform::IpcReceiveResult>::success(std::move(result));
  }

  dasall::platform::PlatformResult<dasall::platform::PeerIdentitySnapshot>
  describe_peer(const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::PeerIdentitySnapshot>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "describe_peer unused in daemon data source contract tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    ++close_count;
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

[[nodiscard]] std::optional<std::string> find_metadata_value(
    const std::vector<std::pair<std::string, std::string>>& metadata,
    std::string_view key) {
  for (const auto& [entry_key, entry_value] : metadata) {
    if (entry_key == key) {
      return entry_value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-daemon-data-source.sock";
  return options;
}

[[nodiscard]] TuiOpenSessionRequest make_open_request() {
  return TuiOpenSessionRequest{
      .profile_id = std::string("desktop_full"),
      .startup_mode_hint = std::string("full"),
      .request_id = "req-open-023",
      .trace_id = "trace-open-023",
  };
}

[[nodiscard]] TuiSubmitTurnRequest make_submit_request() {
  NextTurnPreference preference;
  preference.mode = TuiRoutePreferenceMode::PreferDepth;
  preference.preferred_depth_tier = std::string("deep");
  preference.user_visible_summary = "prefer depth for the next turn";
  preference.source = "daemon_data_source_contract_test";

  return TuiSubmitTurnRequest{
      .session_id = "session-023",
      .user_input = "Summarize the daemon-backed session state",
      .next_preference = std::move(preference),
      .request_id = "req-submit-023",
      .trace_id = "trace-submit-023",
  };
}

[[nodiscard]] TuiPollEventsRequest make_poll_request() {
  return TuiPollEventsRequest{
      .session_id = "session-023",
      .event_cursor = std::string("cursor-022"),
      .request_id = "req-poll-023",
      .trace_id = "trace-poll-023",
  };
}

[[nodiscard]] TuiRouteCatalogRequest make_route_request() {
  return TuiRouteCatalogRequest{
      .session_id = std::string("session-023"),
      .profile_id = std::string("desktop_full"),
      .selector_mode = std::string("next_turn"),
      .request_id = "req-route-023",
      .trace_id = "trace-route-023",
  };
}

[[nodiscard]] TuiCloseSessionRequest make_close_request() {
  return TuiCloseSessionRequest{
      .session_id = "session-023",
      .close_reason = "/exit",
      .request_id = "req-close-023",
      .trace_id = "trace-close-023",
  };
}

[[nodiscard]] std::optional<dasall::tui::ipc::TuiIpcRequestEnvelope>
decode_sent_envelope(const std::vector<std::string>& payloads, const std::size_t index) {
  if (index >= payloads.size()) {
    return std::nullopt;
  }

  return dasall::tui::ipc::test::decode_request_envelope_for_test(payloads.at(index));
}

void daemon_data_source_preserves_projection_contracts_across_all_operations() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiSessionView session;
  session.session_id = "session-023";
  session.profile_id = "desktop_full";
  session.daemon_readiness = "ready";
  session.startup_mode = "full";
  session.started_at = "2026-05-23T14:23:00Z";

  TuiTurnReceipt receipt;
  receipt.request_id = "req-submit-023";
  receipt.trace_id = "trace-submit-023";
  receipt.session_id = session.session_id;
  receipt.disposition = "accepted_async";
  receipt.receipt_ref = "receipt-023";
  receipt.submitted_at = "2026-05-23T14:23:01Z";
  receipt.summary_text = "queued for daemon-backed execution";

  TuiStatusProjection status;
  status.stage = "planning";
  status.current_tool = "knowledge.query";
  status.pending_interaction = "none";
  status.budget_summary = "budget ok";
  status.recovery_summary = "stable";
  status.health_summary = "healthy";
  status.safe_mode_summary = "normal";

  TuiToolSummaryView tool_summary;
  tool_summary.tool_name = "knowledge.query";
  tool_summary.risk_summary = "low";
  tool_summary.observation_summary = "2 memory hits";
  tool_summary.latency_ms = 42;
  tool_summary.badges = {"read_only"};

  TuiEventProjection event;
  event.event_cursor = "cursor-023";
  event.event_kind = "status.updated";
  event.session_id = session.session_id;
  event.timestamp = "2026-05-23T14:23:02Z";
  event.status_delta = status;
  event.tool_summary = tool_summary;
  event.banner_reason = std::string("daemon_data_source_busy");

  TuiRouteCatalogEntry entry;
  entry.provider_id = "deepseek-prod";
  entry.model_id = "deepseek-chat";
  entry.depth_tier = "deep";
  entry.selectable = true;

  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = "deepseek-prod",
      .current_model_id = "deepseek-chat",
      .current_depth_tier = "deep",
      .disabled_reasons = {},
      .next_preference = NextTurnPreference{
          .mode = TuiRoutePreferenceMode::Auto,
          .preferred_depth_tier = std::nullopt,
          .pinned_provider_id = std::nullopt,
          .pinned_model_id = std::nullopt,
          .user_visible_summary = "auto",
          .source = "daemon",
          .applies_to_next_turn_only = true,
      },
  };
  route_catalog.candidate_routes.push_back(entry);

  TuiIpcResponseEnvelope open_response;
  open_response.operation = TuiIpcOperation::OpenSession;
  open_response.request_id = "req-open-023";
  open_response.trace_id = "trace-open-023";
  open_response.session_id = session.session_id;
  open_response.outcome = TuiIpcOutcome::Success;
  open_response.payload = session;

  TuiIpcResponseEnvelope submit_response;
  submit_response.operation = TuiIpcOperation::SubmitTurn;
  submit_response.request_id = receipt.request_id;
  submit_response.trace_id = receipt.trace_id;
  submit_response.session_id = receipt.session_id;
  submit_response.outcome = TuiIpcOutcome::Success;
  submit_response.payload = receipt;

  TuiIpcResponseEnvelope poll_response;
  poll_response.operation = TuiIpcOperation::PollEvents;
  poll_response.request_id = "req-poll-023";
  poll_response.trace_id = "trace-poll-023";
  poll_response.session_id = session.session_id;
  poll_response.outcome = TuiIpcOutcome::Success;
  poll_response.payload = dasall::tui::ipc::TuiIpcPollEventsBatch{
      .events = {event},
      .next_cursor = std::string("cursor-023"),
  };

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "req-route-023";
  route_response.trace_id = "trace-route-023";
  route_response.session_id = session.session_id;
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = route_catalog;

  TuiIpcResponseEnvelope close_response;
  close_response.operation = TuiIpcOperation::CloseSession;
  close_response.request_id = "req-close-023";
  close_response.trace_id = "trace-close-023";
  close_response.session_id = session.session_id;
  close_response.outcome = TuiIpcOutcome::Success;
  close_response.payload = TuiIpcCloseSessionAck{.closed = true};

  ipc->response_texts = {
      dasall::tui::ipc::test::encode_response_envelope_for_test(open_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(submit_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(poll_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(route_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(close_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  DaemonTuiDataSource data_source(make_options());

  const auto open_result = data_source.open_session(make_open_request());
  const auto submit_result = data_source.submit_turn(make_submit_request());
  const auto poll_result = data_source.poll_events(make_poll_request());
  const auto route_result = data_source.route_catalog(make_route_request());
  const auto close_result = data_source.close_session(make_close_request());

  assert_true(open_result.ok() && open_result.has_consistent_values(),
              "daemon data source should preserve session projection on open_session success");
  assert_equal(std::string("session-023"),
               open_result.session->session_id,
               "open_session should surface the daemon session id unchanged");

  assert_true(submit_result.ok() && submit_result.has_consistent_values(),
              "daemon data source should preserve the turn receipt on submit_turn success");
  assert_equal(std::string("receipt-023"),
               submit_result.receipt->receipt_ref,
               "submit_turn should preserve receipt_ref from the daemon response");

  assert_true(poll_result.ok() && poll_result.has_consistent_values(),
              "daemon data source should preserve event batches on poll_events success");
  assert_equal(std::string("cursor-023"),
               poll_result.next_cursor.value_or(std::string()),
               "poll_events should surface the daemon next cursor unchanged");
  assert_true(poll_result.events.front().tool_summary.has_value(),
              "poll_events should preserve tool summary projections");

  assert_true(route_result.ok() && route_result.has_consistent_values(),
              "daemon data source should preserve route catalog projections on success");
  assert_equal(std::string("deepseek-chat"),
               route_result.route_catalog->current_route.current_model_id,
               "route_catalog should surface the daemon current route unchanged");

  assert_true(close_result.ok() && close_result.has_consistent_values(),
              "daemon data source should preserve close acknowledgements on success");
  assert_true(ipc->close_count == 5U,
              "daemon data source should let the controller close every IPC channel roundtrip");

  const auto open_envelope = decode_sent_envelope(ipc->sent_payloads, 0);
  const auto submit_envelope = decode_sent_envelope(ipc->sent_payloads, 1);
  const auto route_envelope = decode_sent_envelope(ipc->sent_payloads, 3);
  const auto close_envelope = decode_sent_envelope(ipc->sent_payloads, 4);

  assert_true(open_envelope.has_value() &&
                  open_envelope->operation == TuiIpcOperation::OpenSession,
              "open_session should still emit the canonical open_session IPC operation");
  const auto* open_payload =
      std::get_if<TuiIpcOpenSessionPayload>(&open_envelope->payload);
  assert_true(open_payload != nullptr,
              "open_session should preserve the open payload variant through the daemon data source seam");
  assert_equal(std::string("desktop_full"),
               open_payload->profile_id.value_or(std::string()),
               "open_session should preserve profile hints through the daemon data source seam");

  assert_true(submit_envelope.has_value() &&
                  submit_envelope->operation == TuiIpcOperation::SubmitTurn,
              "submit_turn should still emit the canonical submit_turn IPC operation");
  assert_equal(std::string("session-023"),
               submit_envelope->session_id.value_or(std::string()),
               "submit_turn should preserve the foreground session id through the daemon data source seam");

  assert_true(route_envelope.has_value() &&
                  route_envelope->operation == TuiIpcOperation::RouteCatalog,
              "route_catalog should reuse the controller route_catalog IPC operation");
  assert_equal(std::string("next_turn"),
               std::get<dasall::tui::ipc::TuiIpcRouteCatalogPayload>(route_envelope->payload)
                   .selector_mode.value_or(std::string()),
               "route_catalog should preserve selector_mode hints through the daemon data source seam");

  assert_true(close_envelope.has_value() &&
                  close_envelope->operation == TuiIpcOperation::CloseSession,
              "close_session should still emit the canonical close_session IPC operation");
  const auto* close_payload =
      std::get_if<TuiIpcCloseSessionPayload>(&close_envelope->payload);
  assert_true(close_payload != nullptr,
              "close_session should preserve the close payload variant through the daemon data source seam");
  assert_equal(std::string("/exit"),
               close_payload->close_reason,
               "close_session should preserve the caller-visible close reason");
}

void daemon_data_source_stays_fail_closed_when_transport_or_session_close_are_unavailable() {
  {
    auto ipc = std::make_shared<ScriptedIpc>();
    ipc->connect_error = make_platform_error(
        dasall::platform::PlatformErrorCode::NotFound,
        dasall::platform::PlatformErrorCategory::IPC,
        "connect() failed for socket path '/tmp/dasall-tui-daemon-data-source.sock': No such file or directory",
        false,
        "connect",
        2);

    const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
    DaemonTuiDataSource data_source(make_options());
    const auto result = data_source.open_session(make_open_request());

    assert_true(!result.ok() && result.has_consistent_values(),
                "daemon data source should keep socket-missing transport failures machine-readable");
    assert_equal(std::string("transport"),
                 result.issue->reason_domain,
                 "transport failures should stay in the transport domain through daemon data source");
    assert_equal(std::string("socket_missing"),
                 result.issue->reason_code,
                 "socket-missing should stay distinguishable from other startup failures");
    assert_equal(std::string("/tmp/dasall-tui-daemon-data-source.sock"),
                 find_metadata_value(result.issue->metadata, "socket_path")
                     .value_or(std::string()),
                 "socket-missing failures should preserve socket_path metadata");
  }

  {
    auto ipc = std::make_shared<ScriptedIpc>();
    TuiIpcResponseEnvelope close_failure;
    close_failure.operation = TuiIpcOperation::CloseSession;
    close_failure.request_id = "req-close-023";
    close_failure.trace_id = "trace-close-023";
    close_failure.session_id = std::string("session-023");
    close_failure.outcome = TuiIpcOutcome::Failure;
    close_failure.reason_domain = std::string("session");
    close_failure.reason_code = std::string("close_unavailable");
    close_failure.message =
        std::string("foreground session close is not yet exposed by the daemon owner");
    close_failure.retryable = false;
    close_failure.error_ref = std::string("close-023");
    close_failure.metadata.emplace_back("close_reason", "/exit");
    ipc->response_texts.push_back(
        dasall::tui::ipc::test::encode_response_envelope_for_test(close_failure));

    const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
    DaemonTuiDataSource data_source(make_options());
    const auto result = data_source.close_session(make_close_request());

    assert_true(!result.ok() && result.has_consistent_values(),
                "daemon data source should preserve close_unavailable while BLK-TUI-007 remains open downstream");
    assert_equal(std::string("session"),
                 result.issue->reason_domain,
                 "close_unavailable should remain a session-scoped failure through daemon data source");
    assert_equal(std::string("close_unavailable"),
                 result.issue->reason_code,
                 "close_unavailable should remain machine-readable for later app/session lifecycle handling");
    assert_true(!result.closed,
                "close_session should stay fail-closed when the daemon owner reports close_unavailable");
  }
}

void daemon_data_source_files_stay_on_tui_projection_and_controller_boundary() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_DAEMON_DATA_SOURCE_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_DAEMON_DATA_SOURCE_IMPL});

  assert_true(header_text.find("#include \"access/") == std::string::npos,
              "DaemonTuiDataSource should not include access private headers");
  assert_true(header_text.find("#include \"runtime/") == std::string::npos,
              "DaemonTuiDataSource should not include runtime private headers");
  assert_true(header_text.find("#include \"llm/") == std::string::npos,
              "DaemonTuiDataSource should not include llm private headers");
  assert_true(header_text.find("#include \"profiles/") == std::string::npos,
              "DaemonTuiDataSource should not include profile private headers");
  assert_true(header_text.find("#include \"apps/") == std::string::npos,
              "DaemonTuiDataSource should not include app-private headers from other entrypoints");
  assert_true(header_text.find("DaemonClientResponse") == std::string::npos,
              "DaemonTuiDataSource should not bind to CLI private daemon response carriers");
  assert_true(header_text.find("UdsResponseFrame") == std::string::npos,
              "DaemonTuiDataSource should not bind to raw daemon UDS carriers");

  assert_true(impl_text.find("#include \"IIPC.h\"") == std::string::npos,
              "DaemonTuiDataSource should not talk to platform IPC directly");
  assert_true(impl_text.find("UnixIpcProvider") == std::string::npos,
              "DaemonTuiDataSource should stay above raw IPC provider wiring");
  assert_true(impl_text.find("socket(") == std::string::npos,
              "DaemonTuiDataSource should not create sockets directly");
  assert_true(impl_text.find("connect(") == std::string::npos,
              "DaemonTuiDataSource should not connect to transports directly");
  assert_true(impl_text.find("AgentRequest") == std::string::npos,
              "DaemonTuiDataSource should not bind to access shared request owners");
  assert_true(impl_text.find("RuntimeDispatchRequest") == std::string::npos,
              "DaemonTuiDataSource should not bind to runtime dispatch owners");
}

}  // namespace

int main() {
  try {
    daemon_data_source_preserves_projection_contracts_across_all_operations();
    daemon_data_source_stays_fail_closed_when_transport_or_session_close_are_unavailable();
    daemon_data_source_files_stay_on_tui_projection_and_controller_boundary();
  } catch (const std::exception& exception) {
    std::cerr << "[DaemonTuiDataSourceContractTest] FAILED: " << exception.what()
              << '\n';
    return 1;
  }

  return 0;
}