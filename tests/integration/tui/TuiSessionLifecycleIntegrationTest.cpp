#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PlatformError.h"
#include "app/TuiApp.h"
#include "command/TuiSlashCommandParser.h"
#include "data/DaemonTuiDataSource.h"
#include "ipc/TuiIpcController.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::app::TuiApp;
using dasall::tui::app::TuiAppOptions;
using dasall::tui::command::TuiSlashCommandParser;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiModelRouteProjection;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSessionView;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::ipc::TuiIpcCloseSessionAck;
using dasall::tui::ipc::TuiIpcCloseSessionPayload;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcPollEventsBatch;
using dasall::tui::ipc::TuiIpcRequestEnvelope;
using dasall::tui::ipc::TuiIpcResponseEnvelope;
using dasall::tui::terminal::TuiTerminalProbeEnvironment;

[[nodiscard]] dasall::platform::PlatformError make_platform_error(
    const std::string& detail) {
  return dasall::platform::PlatformError{
      .code = dasall::platform::PlatformErrorCode::InvalidArgument,
      .category = dasall::platform::PlatformErrorCategory::Validation,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = detail,
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
  std::vector<std::string> response_texts;
  std::vector<std::string> sent_payloads;
  std::size_t receive_index = 0;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(
        make_platform_error("listen unused in session lifecycle integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(
        make_platform_error("accept unused in session lifecycle integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 127U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    sent_payloads.emplace_back(reinterpret_cast<const char*>(payload.data()),
                               payload.size());
    return dasall::platform::PlatformResult<
        dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
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
        dasall::platform::PeerIdentitySnapshot>::failure(
        make_platform_error(
            "describe_peer unused in session lifecycle integration tests"));
  }

  dasall::platform::PlatformResult<bool> close(
      const dasall::platform::IpcChannelHandle&) override {
    return dasall::platform::PlatformResult<bool>::success(true);
  }
};

[[nodiscard]] TuiTerminalProbeEnvironment make_full_screen_environment() {
  TuiTerminalProbeEnvironment environment;
  environment.stdin_is_tty = true;
  environment.stdout_is_tty = true;
  environment.stderr_is_tty = true;
  environment.term = "xterm-256color";
  environment.locale = "en_US.UTF-8";
  environment.columns = 120;
  environment.rows = 36;
  environment.utf8_enabled = true;
  environment.bracketed_paste_supported = true;
  environment.resize_events_supported = true;
  environment.external_editor_available = true;
  return environment;
}

[[nodiscard]] TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-session-lifecycle.sock";
  return options;
}

[[nodiscard]] TuiSessionView make_session(std::string session_id,
                                          std::string started_at) {
  return TuiSessionView{
      .session_id = std::move(session_id),
      .profile_id = "desktop_full",
      .daemon_readiness = "accepted",
      .startup_mode = "full",
      .started_at = std::move(started_at),
  };
}

[[nodiscard]] TuiRouteCatalogView make_route_catalog(std::string provider_id,
                                                     std::string model_id,
                                                     std::string depth_tier) {
  TuiRouteCatalogEntry entry;
  entry.provider_id = provider_id;
  entry.model_id = model_id;
  entry.depth_tier = depth_tier;
  entry.selectable = true;

  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = std::move(provider_id),
      .current_model_id = std::move(model_id),
      .current_depth_tier = std::move(depth_tier),
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
  return route_catalog;
}

[[nodiscard]] TuiEventProjection make_transcript_event(std::string session_id) {
  TuiStatusProjection status;
  status.stage = "tool_calling";
  status.current_tool = "tool.search";
  status.pending_interaction = "confirm replay";
  status.budget_summary = "Budget 58% remaining";
  status.recovery_summary = "Awaiting operator confirmation";
  status.health_summary = "degraded";
  status.safe_mode_summary = "guarded";

  TuiTurnReceipt receipt;
  receipt.request_id = "receipt-session-lifecycle";
  receipt.trace_id = "trace-receipt-session-lifecycle";
  receipt.session_id = session_id;
  receipt.disposition = "accepted_async";
  receipt.receipt_ref = "receipt-ref-session-lifecycle";
  receipt.submitted_at = "2026-05-23T19:00:01Z";
  receipt.summary_text = "stale transcript should disappear";
  receipt.reason_code = std::nullopt;

  TuiEventProjection event;
  event.event_cursor = "cursor-session-lifecycle-001";
  event.event_kind = "turn_receipt";
  event.session_id = std::move(session_id);
  event.timestamp = "2026-05-23T19:00:01Z";
  event.status_delta = status;
  event.turn_receipt = receipt;
  event.tool_summary = std::nullopt;
  event.banner_reason = std::string("stale banner should disappear");
  return event;
}

[[nodiscard]] std::string encode_response(
    const TuiIpcResponseEnvelope& envelope) {
  return dasall::tui::ipc::test::encode_response_envelope_for_test(envelope);
}

[[nodiscard]] std::optional<TuiIpcRequestEnvelope> decode_sent_envelope(
    const std::vector<std::string>& payloads,
    const std::size_t index) {
  if (index >= payloads.size()) {
    return std::nullopt;
  }

  return dasall::tui::ipc::test::decode_request_envelope_for_test(payloads.at(index));
}

void tui_clear_rebinds_a_new_foreground_session_and_keeps_close_failure_visible() {
  auto ipc = std::make_shared<ScriptedIpc>();
  const TuiSessionView initial_session =
      make_session("session-clear-001", "2026-05-23T19:00:00Z");
  const TuiSessionView rebound_session =
      make_session("session-clear-002", "2026-05-23T19:00:02Z");

  TuiIpcResponseEnvelope open_initial_response;
  open_initial_response.operation = TuiIpcOperation::OpenSession;
  open_initial_response.request_id = "open_session-session_lifecycle-1";
  open_initial_response.trace_id = "trace-open_session-session_lifecycle-2";
  open_initial_response.session_id = initial_session.session_id;
  open_initial_response.outcome = TuiIpcOutcome::Success;
  open_initial_response.payload = initial_session;

  TuiIpcResponseEnvelope route_initial_response;
  route_initial_response.operation = TuiIpcOperation::RouteCatalog;
  route_initial_response.request_id = "route_catalog-session_lifecycle-3";
  route_initial_response.trace_id = "trace-route_catalog-session_lifecycle-4";
  route_initial_response.session_id = initial_session.session_id;
  route_initial_response.outcome = TuiIpcOutcome::Success;
  route_initial_response.payload =
      make_route_catalog("provider-old", "model-old", "balanced");

  TuiIpcResponseEnvelope poll_response;
  poll_response.operation = TuiIpcOperation::PollEvents;
  poll_response.request_id = "poll_events-session_lifecycle-5";
  poll_response.trace_id = "trace-poll_events-session_lifecycle-6";
  poll_response.session_id = initial_session.session_id;
  poll_response.outcome = TuiIpcOutcome::Success;
  poll_response.payload = TuiIpcPollEventsBatch{
      .events = {make_transcript_event(initial_session.session_id)},
      .next_cursor = std::string("cursor-session-lifecycle-001"),
  };

  TuiIpcResponseEnvelope clear_close_failure;
  clear_close_failure.operation = TuiIpcOperation::CloseSession;
  clear_close_failure.request_id = "close_session-session_lifecycle-7";
  clear_close_failure.trace_id = "trace-close_session-session_lifecycle-8";
  clear_close_failure.session_id = initial_session.session_id;
  clear_close_failure.outcome = TuiIpcOutcome::Failure;
  clear_close_failure.reason_domain = std::string("session");
  clear_close_failure.reason_code = std::string("close_unavailable");
  clear_close_failure.message =
      std::string("foreground session close is not yet exposed by the daemon owner");
  clear_close_failure.retryable = false;
  clear_close_failure.error_ref = std::string("close-clear-026");
  clear_close_failure.metadata.emplace_back("close_reason", "/clear");

  TuiIpcResponseEnvelope open_rebound_response;
  open_rebound_response.operation = TuiIpcOperation::OpenSession;
  open_rebound_response.request_id = "open_session-session_lifecycle-9";
  open_rebound_response.trace_id = "trace-open_session-session_lifecycle-10";
  open_rebound_response.session_id = rebound_session.session_id;
  open_rebound_response.outcome = TuiIpcOutcome::Success;
  open_rebound_response.payload = rebound_session;

  TuiIpcResponseEnvelope route_rebound_response;
  route_rebound_response.operation = TuiIpcOperation::RouteCatalog;
  route_rebound_response.request_id = "route_catalog-session_lifecycle-11";
  route_rebound_response.trace_id = "trace-route_catalog-session_lifecycle-12";
  route_rebound_response.session_id = rebound_session.session_id;
  route_rebound_response.outcome = TuiIpcOutcome::Success;
  route_rebound_response.payload =
      make_route_catalog("provider-new", "model-new", "deep");

  TuiIpcResponseEnvelope shutdown_close_response;
  shutdown_close_response.operation = TuiIpcOperation::CloseSession;
  shutdown_close_response.request_id = "close_session-session_lifecycle-13";
  shutdown_close_response.trace_id = "trace-close_session-session_lifecycle-14";
  shutdown_close_response.session_id = rebound_session.session_id;
  shutdown_close_response.outcome = TuiIpcOutcome::Success;
  shutdown_close_response.payload = TuiIpcCloseSessionAck{.closed = true};

  ipc->response_texts = {
      encode_response(open_initial_response),
      encode_response(route_initial_response),
      encode_response(poll_response),
      encode_response(clear_close_failure),
      encode_response(open_rebound_response),
      encode_response(route_rebound_response),
      encode_response(shutdown_close_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  const TuiSlashCommandParser parser;
  const auto clear_result = parser.parse("/clear");
  assert_true(clear_result.accepted,
              "/clear should stay available as a typed local action for the session lifecycle test");

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "session_lifecycle";
  options.probe_environment = make_full_screen_environment();
  options.bootstrap_tick_count = 1;
  options.initial_draft = std::string("stale draft");
  options.print_final_screen = false;
  options.scripted_actions.push_back(clear_result.to_action());
  options.data_source_override = std::make_unique<DaemonTuiDataSource>(make_options());

  const int exit_code = app.run(std::move(options));
  const std::string& screen = app.last_rendered_screen();

  assert_equal(0, exit_code,
               "foreground clear should still let the app exit cleanly after rebinding a new session");
  assert_true(app.shutdown_clean(),
              "foreground clear should leave the rebound session closable during shutdown");
  assert_equal(rebound_session.session_id,
               app.screen_model().session.session_id,
               "foreground clear should rebind the screen model to the new foreground session");
  assert_true(app.screen_model().transcript.empty(),
              "foreground clear should clear the previous transcript before the next turn starts");
  assert_true(app.screen_model().composer.text.empty(),
              "foreground clear should clear the current composer draft");
  assert_equal(std::string("ready"),
               app.screen_model().composer.mode,
               "foreground clear should return the composer to ready mode");
  assert_equal(std::string("provider-new"),
               app.screen_model().route.current_provider_id,
               "foreground clear should refresh the route projection for the rebound session");
  assert_equal(1,
               static_cast<int>(app.screen_model().banners.size()),
               "foreground clear should keep the close_unavailable failure visible after rebinding");
  assert_true(app.screen_model().banners.front().reason_code.has_value(),
              "foreground clear should preserve the stable close failure reason code");
  assert_equal(std::string("close_unavailable"),
               *app.screen_model().banners.front().reason_code,
               "foreground clear should surface close_unavailable instead of pretending the old session closed");
  assert_true(screen.find("SESSION session-clear-002") != std::string::npos,
              "foreground clear should render the rebound session id in the terminal frame");
  assert_true(screen.find("stale transcript should disappear") == std::string::npos,
              "foreground clear should remove the previous transcript from the rendered frame");
  assert_true(screen.find("stale draft") == std::string::npos,
              "foreground clear should remove the previous draft from the rendered frame");

  const auto clear_close_envelope = decode_sent_envelope(ipc->sent_payloads, 3);
  const auto shutdown_close_envelope = decode_sent_envelope(ipc->sent_payloads, 6);
  assert_true(clear_close_envelope.has_value() &&
                  clear_close_envelope->operation == TuiIpcOperation::CloseSession,
              "foreground clear should emit a close_session request for the previous session");
  assert_true(shutdown_close_envelope.has_value() &&
                  shutdown_close_envelope->operation == TuiIpcOperation::CloseSession,
              "shutdown after foreground clear should still close the rebound session");

  const auto* clear_close_payload =
      std::get_if<TuiIpcCloseSessionPayload>(&clear_close_envelope->payload);
  const auto* shutdown_close_payload =
      std::get_if<TuiIpcCloseSessionPayload>(&shutdown_close_envelope->payload);
  assert_true(clear_close_payload != nullptr,
              "foreground clear should preserve the close_session payload variant");
  assert_true(shutdown_close_payload != nullptr,
              "shutdown should preserve the close_session payload variant after rebinding");
  assert_equal(std::string("/clear"),
               clear_close_payload->close_reason,
               "foreground clear should preserve the caller-visible /clear close reason");
  assert_equal(std::string("prototype_round_complete"),
               shutdown_close_payload->close_reason,
               "shutdown should keep the default prototype close reason after a successful clear rebind");
}

void tui_exit_requests_close_with_the_explicit_exit_reason() {
  auto ipc = std::make_shared<ScriptedIpc>();
  const TuiSessionView session = make_session("session-exit-001", "2026-05-23T19:30:00Z");

  TuiIpcResponseEnvelope open_response;
  open_response.operation = TuiIpcOperation::OpenSession;
  open_response.request_id = "open_session-session_exit-1";
  open_response.trace_id = "trace-open_session-session_exit-2";
  open_response.session_id = session.session_id;
  open_response.outcome = TuiIpcOutcome::Success;
  open_response.payload = session;

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "route_catalog-session_exit-3";
  route_response.trace_id = "trace-route_catalog-session_exit-4";
  route_response.session_id = session.session_id;
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = make_route_catalog("provider-exit", "model-exit", "balanced");

  TuiIpcResponseEnvelope close_response;
  close_response.operation = TuiIpcOperation::CloseSession;
  close_response.request_id = "close_session-session_exit-5";
  close_response.trace_id = "trace-close_session-session_exit-6";
  close_response.session_id = session.session_id;
  close_response.outcome = TuiIpcOutcome::Success;
  close_response.payload = TuiIpcCloseSessionAck{.closed = true};

  ipc->response_texts = {
      encode_response(open_response),
      encode_response(route_response),
      encode_response(close_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);
  const TuiSlashCommandParser parser;
  const auto exit_result = parser.parse("/exit");
  assert_true(exit_result.accepted,
              "/exit should stay available as a typed local action for the session lifecycle test");

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "session_exit";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.scripted_actions.push_back(exit_result.to_action());
  options.data_source_override = std::make_unique<DaemonTuiDataSource>(make_options());

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "/exit should let the app close the foreground session cleanly when the owner acknowledges close_session");
  assert_true(app.shutdown_clean(),
              "successful /exit should report a clean shutdown");
  assert_true(app.last_error().empty(),
              "successful /exit should not keep a terminal close error string");

  const auto close_envelope = decode_sent_envelope(ipc->sent_payloads, 2);
  assert_true(close_envelope.has_value() &&
                  close_envelope->operation == TuiIpcOperation::CloseSession,
              "/exit should emit the canonical close_session IPC operation");
  const auto* close_payload =
      std::get_if<TuiIpcCloseSessionPayload>(&close_envelope->payload);
  assert_true(close_payload != nullptr,
              "/exit should preserve the close_session payload variant");
  assert_equal(std::string("/exit"),
               close_payload->close_reason,
               "/exit should preserve the explicit caller-visible exit close reason");
}

}  // namespace

int main() {
  try {
    tui_clear_rebinds_a_new_foreground_session_and_keeps_close_failure_visible();
    tui_exit_requests_close_with_the_explicit_exit_reason();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiSessionLifecycleIntegrationTest] FAILED: "
              << exception.what() << '\n';
    return 1;
  }

  return 0;
}