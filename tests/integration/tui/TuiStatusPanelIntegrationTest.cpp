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
#include "data/DaemonTuiDataSource.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::app::TuiApp;
using dasall::tui::app::TuiAppOptions;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiModelRouteProjection;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSessionView;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::data::TuiToolSummaryView;
using dasall::tui::ipc::TuiIpcCloseSessionAck;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcPollEventsBatch;
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
  std::size_t receive_index = 0;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(
      make_platform_error("listen unused in status panel integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(
      make_platform_error("accept unused in status panel integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> connect(
      const dasall::platform::IpcEndpoint&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::success(
        dasall::platform::IpcChannelHandle{.native_fd = 126U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
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
      make_platform_error("describe_peer unused in status panel integration tests"));
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
  options.socket_path = "/tmp/dasall-tui-status-panel.sock";
  return options;
}

void tui_status_panel_integration_refreshes_polled_status_projection() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiSessionView session;
  session.session_id = "session-025";
  session.profile_id = "desktop_full";
  session.daemon_readiness = "accepted";
  session.startup_mode = "full";
  session.started_at = "2026-05-23T16:25:00Z";

  TuiRouteCatalogEntry entry;
  entry.provider_id = "deepseek-prod";
  entry.model_id = "deepseek-chat";
  entry.depth_tier = "balanced";
  entry.selectable = true;

  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = "deepseek-prod",
      .current_model_id = "deepseek-chat",
      .current_depth_tier = "balanced",
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

  TuiStatusProjection status;
  status.stage = "tool_calling";
  status.current_tool = "tool.search";
  status.pending_interaction = "confirm external tool";
  status.budget_summary = "Budget 58% remaining";
  status.recovery_summary = "Accepted safe replay window after tool timeout.";
  status.health_summary = "degraded";
  status.safe_mode_summary = "guarded";

  TuiToolSummaryView tool_summary;
  tool_summary.tool_name = "tool.search";
  tool_summary.risk_summary = "elevated network latency";
  tool_summary.observation_summary =
      "Collected three planning references for the next turn.";
  tool_summary.latency_ms = 237;
  tool_summary.badges = {"read_only", "slow_path"};

  TuiEventProjection event;
  event.event_cursor = "cursor-025";
  event.event_kind = "status.updated";
  event.session_id = session.session_id;
  event.timestamp = "2026-05-23T16:25:01Z";
  event.status_delta = status;
  event.tool_summary = tool_summary;
  event.banner_reason = std::string("operator attention required");

  TuiIpcResponseEnvelope open_response;
  open_response.operation = TuiIpcOperation::OpenSession;
  open_response.request_id = "open_session-status_projection-1";
  open_response.trace_id = "trace-open_session-status_projection-2";
  open_response.session_id = session.session_id;
  open_response.outcome = TuiIpcOutcome::Success;
  open_response.payload = session;

  TuiIpcResponseEnvelope route_response;
  route_response.operation = TuiIpcOperation::RouteCatalog;
  route_response.request_id = "route_catalog-status_projection-3";
  route_response.trace_id = "trace-route_catalog-status_projection-4";
  route_response.session_id = session.session_id;
  route_response.outcome = TuiIpcOutcome::Success;
  route_response.payload = route_catalog;

  TuiIpcResponseEnvelope poll_response;
  poll_response.operation = TuiIpcOperation::PollEvents;
  poll_response.request_id = "poll_events-status_projection-5";
  poll_response.trace_id = "trace-poll_events-status_projection-6";
  poll_response.session_id = session.session_id;
  poll_response.outcome = TuiIpcOutcome::Success;
  poll_response.payload = TuiIpcPollEventsBatch{
      .events = {event},
      .next_cursor = std::string("cursor-025"),
  };

  TuiIpcResponseEnvelope close_response;
  close_response.operation = TuiIpcOperation::CloseSession;
  close_response.request_id = "close_session-status_projection-7";
  close_response.trace_id = "trace-close_session-status_projection-8";
  close_response.session_id = session.session_id;
  close_response.outcome = TuiIpcOutcome::Success;
  close_response.payload = TuiIpcCloseSessionAck{.closed = true};

  ipc->response_texts = {
      dasall::tui::ipc::test::encode_response_envelope_for_test(open_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(route_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(poll_response),
      dasall::tui::ipc::test::encode_response_envelope_for_test(close_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "status_projection";
  options.probe_environment = make_full_screen_environment();
  options.bootstrap_tick_count = 1;
  options.print_final_screen = false;
  options.data_source_override = std::make_unique<DaemonTuiDataSource>(make_options());

  const int exit_code = app.run(std::move(options));
  const std::string& screen = app.last_rendered_screen();

  assert_equal(0, exit_code,
               "status panel integration should exit cleanly after one daemon-backed poll");
  assert_true(app.shutdown_clean(),
              "status panel integration should close the daemon-backed session cleanly");
  assert_equal(std::string("tool_calling"),
               app.screen_model().status.stage,
               "screen model should retain the full refreshed stage projection after polling");
  assert_equal(std::string("tool.search"),
               app.screen_model().status.current_tool,
               "screen model should retain the full refreshed current tool after polling");
  assert_equal(std::string("Accepted safe replay window after tool timeout."),
               app.screen_model().status.recovery_summary,
               "screen model should retain the full refreshed recovery summary after polling");
  assert_true(screen.find("stage: [tool calling | guarded]") != std::string::npos,
              "status panel should refresh the stage badge from polled status projection");
  assert_true(screen.find("tool: tool.search") != std::string::npos,
              "status panel should refresh the current tool from polled status projection");
  assert_true(screen.find("pending: confirm external tool") != std::string::npos,
              "status panel should refresh the pending interaction from polled status projection");
  assert_true(screen.find("budget: Budget 58% remaining") != std::string::npos,
              "status panel should refresh the budget summary from polled status projection");
  assert_true(screen.find("recovery: Accepted safe replay") != std::string::npos,
              "status panel should refresh the recovery summary from polled status projection");
  assert_true(screen.find("safe mode: guarded") != std::string::npos,
              "status panel should refresh the safe mode summary from polled status projection");
  assert_true(screen.find("decision: awaiting confirm") != std::string::npos,
              "status panel should keep the user-facing decision summary derived from the refreshed status");
  assert_true(screen.find("status_delta") == std::string::npos,
              "status panel should not dump internal event field names into the rendered screen");
  assert_true(screen.find("tool_summary") == std::string::npos,
              "status panel should not dump internal tool summary field names into the rendered screen");
  assert_true(screen.find("current_tool") == std::string::npos,
              "status panel should render projection labels instead of raw struct member names");
}

}  // namespace

int main() {
  try {
    tui_status_panel_integration_refreshes_polled_status_projection();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiStatusPanelIntegrationTest] FAILED: " << exception.what()
              << '\n';
    return 1;
  }

  return 0;
}
