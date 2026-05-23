#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "PlatformError.h"
#include "app/TuiApp.h"
#include "data/DaemonTuiDataSource.h"
#include "ipc/TuiIpcController.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::app::TuiApp;
using dasall::tui::app::TuiAppOptions;
using dasall::tui::data::DaemonTuiDataSource;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSessionView;
using dasall::tui::ipc::TuiIpcCloseSessionAck;
using dasall::tui::ipc::TuiIpcControllerOptions;
using dasall::tui::ipc::TuiIpcOperation;
using dasall::tui::ipc::TuiIpcOutcome;
using dasall::tui::ipc::TuiIpcResponseEnvelope;
using dasall::tui::terminal::TuiStartupMode;
using dasall::tui::terminal::TuiTerminalProbeEnvironment;

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
  std::size_t receive_index = 0;

  dasall::platform::PlatformResult<dasall::platform::IpcListenerHandle> listen(
      const dasall::platform::IpcEndpoint&,
      const dasall::platform::ListenOptions&) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcListenerHandle>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "listen unused in tui startup failure integration tests"));
  }

  dasall::platform::PlatformResult<dasall::platform::IpcChannelHandle> accept(
      const dasall::platform::IpcListenerHandle&,
      std::int32_t) override {
    return dasall::platform::PlatformResult<
        dasall::platform::IpcChannelHandle>::failure(make_platform_error(
        dasall::platform::PlatformErrorCode::InvalidArgument,
        dasall::platform::PlatformErrorCategory::Validation,
        "accept unused in tui startup failure integration tests"));
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
        dasall::platform::IpcChannelHandle{.native_fd = 141U});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcSendResult> send(
      const dasall::platform::IpcChannelHandle&,
      const dasall::platform::IpcPayload& payload) override {
    if (send_error.has_value()) {
      return dasall::platform::PlatformResult<
          dasall::platform::IpcSendResult>::failure(*send_error);
    }

    return dasall::platform::PlatformResult<
        dasall::platform::IpcSendResult>::success(
        dasall::platform::IpcSendResult{.bytes_sent = payload.size()});
  }

  dasall::platform::PlatformResult<dasall::platform::IpcReceiveResult> receive(
      const dasall::platform::IpcChannelHandle&,
      std::int32_t) override {
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
        "describe_peer unused in tui startup failure integration tests"));
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

[[nodiscard]] TuiTerminalProbeEnvironment make_narrow_environment() {
  TuiTerminalProbeEnvironment environment = make_full_screen_environment();
  environment.columns = 80;
  environment.rows = 24;
  return environment;
}

[[nodiscard]] TuiIpcControllerOptions make_options() {
  TuiIpcControllerOptions options;
  options.socket_path = "/tmp/dasall-tui-startup-failure.sock";
  return options;
}

[[nodiscard]] TuiSessionView make_session(std::string profile_id,
                                          std::string startup_mode) {
  return TuiSessionView{
      .session_id = "session-startup-024",
      .profile_id = std::move(profile_id),
      .daemon_readiness = "accepted",
      .startup_mode = std::move(startup_mode),
      .started_at = "2026-05-23T12:24:00Z",
  };
}

[[nodiscard]] std::string encode_response(
    const TuiIpcResponseEnvelope& envelope) {
  return dasall::tui::ipc::test::encode_response_envelope_for_test(envelope);
}

void tui_app_degrades_to_narrow_mode_and_keeps_startup_clean() {
  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "narrow_cjk";
  options.probe_environment = make_narrow_environment();
  options.print_final_screen = false;

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "narrow terminals should degrade to narrow mode instead of failing closed");
  assert_true(app.shutdown_clean(),
              "narrow startup should still close the startup session cleanly");
  assert_true(app.startup_mode() == TuiStartupMode::Narrow,
              "80x24 capable terminals should classify as narrow startup mode");
  assert_equal(std::string("narrow"),
               app.screen_model().session.startup_mode,
               "open_session should preserve the narrow startup mode hint");
  assert_true(app.last_error().empty(),
              "narrow degradation should not keep a startup error string");
}

void tui_app_fail_closes_before_data_source_when_terminal_is_non_tty() {
  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "startup_failure";
  options.probe_environment = TuiTerminalProbeEnvironment{};
  options.print_final_screen = false;

  std::ostringstream captured_output;
  options.output_stream = &captured_output;

  const int exit_code = app.run(std::move(options));

  assert_equal(1, exit_code,
               "non-TTY startup should fail closed before opening any session");
  assert_true(app.startup_mode() == TuiStartupMode::FailClosed,
              "non-TTY startup should stay in fail-closed mode");
  assert_true(!app.shutdown_clean(),
              "non-TTY fail-closed startup should not report a clean shutdown");
  assert_true(std::string(app.last_error()).find("TUI startup blocked") != std::string::npos,
              "non-TTY startup should surface the terminal blocker summary");
  assert_true(captured_output.str().find("stdin is not attached to a TTY") !=
                  std::string::npos,
              "non-TTY startup should print the terminal blocker details");
}

void tui_app_reports_daemon_unavailable_when_open_session_cannot_connect() {
  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->connect_error = make_platform_error(
      dasall::platform::PlatformErrorCode::NotFound,
      dasall::platform::PlatformErrorCategory::IPC,
      "connect() failed for socket path '/tmp/dasall-tui-startup-failure.sock': No such file or directory",
      false,
      "connect",
      2);

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "startup_failure";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.data_source_override = std::make_unique<DaemonTuiDataSource>(make_options());

  std::ostringstream captured_output;
  options.output_stream = &captured_output;

  const int exit_code = app.run(std::move(options));

  assert_equal(1, exit_code,
               "missing daemon socket should fail closed on the daemon-backed startup path");
  assert_true(app.startup_mode() == TuiStartupMode::FullScreen,
              "daemon unavailable should preserve the terminal-derived startup mode");
  assert_true(!app.shutdown_clean(),
              "daemon unavailable before session open should not report a clean shutdown");
  assert_true(app.last_rendered_screen().empty(),
              "daemon unavailable during startup should not render a terminal frame");
  assert_true(std::string(app.last_error()).find("daemon unavailable") != std::string::npos,
              "socket_missing should surface as daemon unavailable to the TUI user surface");
  assert_true(captured_output.str().find("daemon unavailable") != std::string::npos,
              "daemon unavailable startup should print the stable startup issue");
  assert_true(captured_output.str().find("permission denied") == std::string::npos,
              "daemon unavailable startup should stay distinct from permission denied");
}

void tui_app_preserves_permission_denied_as_a_distinct_startup_issue() {
  auto ipc = std::make_shared<ScriptedIpc>();
  ipc->connect_error = make_platform_error(
      dasall::platform::PlatformErrorCode::PermissionDenied,
      dasall::platform::PlatformErrorCategory::IPC,
      "connect() failed for socket path '/tmp/dasall-tui-startup-failure.sock': Permission denied",
      false,
      "connect",
      13);

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "startup_failure";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.data_source_override = std::make_unique<DaemonTuiDataSource>(make_options());

  std::ostringstream captured_output;
  options.output_stream = &captured_output;

  const int exit_code = app.run(std::move(options));

  assert_equal(1, exit_code,
               "permission denied should fail closed on the daemon-backed startup path");
  assert_true(std::string(app.last_error()).find("permission denied") != std::string::npos,
              "permission denied should stay visible in the startup issue text");
  assert_true(std::string(app.last_error()).find("root/sudo-only operator path") !=
                  std::string::npos,
              "permission denied should mention the frozen operator model guidance");
  assert_true(std::string(app.last_error()).find("daemon unavailable") == std::string::npos,
              "permission denied should stay distinct from daemon unavailable");
  assert_true(captured_output.str().find("root/sudo-only operator path") !=
                  std::string::npos,
              "permission denied startup should print operator guidance without implicit escalation");
}

void tui_app_reports_profile_missing_from_route_catalog_startup_path() {
  auto ipc = std::make_shared<ScriptedIpc>();

  TuiIpcResponseEnvelope open_response;
  open_response.operation = TuiIpcOperation::OpenSession;
  open_response.request_id = "open_session-startup_failure-1";
  open_response.trace_id = "trace-open_session-startup_failure-2";
  open_response.session_id = std::string("session-startup-024");
  open_response.outcome = TuiIpcOutcome::Success;
  open_response.payload = make_session("desktop_missing", "full");

  TuiIpcResponseEnvelope route_failure;
  route_failure.operation = TuiIpcOperation::RouteCatalog;
  route_failure.request_id = "route_catalog-startup_failure-3";
  route_failure.trace_id = "trace-route_catalog-startup_failure-4";
  route_failure.session_id = std::string("session-startup-024");
  route_failure.outcome = TuiIpcOutcome::Failure;
  route_failure.reason_domain = std::string("daemon");
  route_failure.reason_code = std::string("profile_missing");
  route_failure.message =
      std::string("profile 'desktop_missing' is missing from the startup projection store");
  route_failure.retryable = false;
  route_failure.error_ref = std::string("profile-024");

  TuiIpcResponseEnvelope close_response;
  close_response.operation = TuiIpcOperation::CloseSession;
  close_response.request_id = "close_session-startup_failure-5";
  close_response.trace_id = "trace-close_session-startup_failure-6";
  close_response.session_id = std::string("session-startup-024");
  close_response.outcome = TuiIpcOutcome::Success;
  close_response.payload = TuiIpcCloseSessionAck{.closed = true};

  ipc->response_texts = {
      encode_response(open_response),
      encode_response(route_failure),
      encode_response(close_response),
  };

  const dasall::tui::ipc::test::ScopedIpcOverride override(ipc);

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "startup_failure";
  options.profile_id = std::string("desktop_missing");
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.data_source_override = std::make_unique<DaemonTuiDataSource>(make_options());

  std::ostringstream captured_output;
  options.output_stream = &captured_output;

  const int exit_code = app.run(std::move(options));

  assert_equal(1, exit_code,
               "profile_missing should fail closed once startup route projection cannot be loaded");
  assert_true(app.startup_mode() == TuiStartupMode::FullScreen,
              "profile_missing should preserve the terminal-derived startup mode");
  assert_true(app.last_rendered_screen().empty(),
              "route-catalog startup failures should not emit a terminal frame before exit");
  assert_true(std::string(app.last_error()).find("profile is missing or incomplete") !=
                  std::string::npos,
              "profile_missing should surface a stable startup explanation");
  assert_true(captured_output.str().find("profile is missing or incomplete") !=
                  std::string::npos,
              "profile_missing startup should print the stable startup issue");
  assert_true(app.shutdown_clean(),
              "route-catalog startup failures should still close the temporary session cleanly");
}

}  // namespace

int main() {
  try {
    tui_app_degrades_to_narrow_mode_and_keeps_startup_clean();
    tui_app_fail_closes_before_data_source_when_terminal_is_non_tty();
    tui_app_reports_daemon_unavailable_when_open_session_cannot_connect();
    tui_app_preserves_permission_denied_as_a_distinct_startup_issue();
    tui_app_reports_profile_missing_from_route_catalog_startup_path();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiAppStartupFailureTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}