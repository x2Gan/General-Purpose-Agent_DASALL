#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include "app/TuiApp.h"
#include "data/FakeTuiDataSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::app::TuiApp;
using dasall::tui::app::TuiAppOptions;
using dasall::tui::terminal::TuiStartupMode;
using dasall::tui::terminal::TuiTerminalProbeEnvironment;

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

void tui_app_starts_full_screen_fake_session() {
  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "golden_ready";
  options.data_source_override =
      std::make_unique<dasall::tui::data::FakeTuiDataSource>(options.scenario_id);
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code, "fake-only startup should exit cleanly on a valid full-screen terminal");
  assert_true(app.shutdown_clean(), "fake-only startup should close the fake session cleanly");
  assert_true(app.startup_mode() == TuiStartupMode::FullScreen,
              "fake-only startup should classify the injected terminal as full-screen capable");
  assert_true(app.last_error().empty(), "successful startup should not keep a startup error string");
  assert_true(app.last_rendered_screen().find("SESSION fake-golden-ready-001") != std::string::npos,
              "startup screen should expose the fake session banner");
  assert_true(app.last_rendered_screen().find("ROUTE provider-openai/gpt-4.1 [balanced] | NEXT auto") != std::string::npos,
              "startup screen should render the current fake route summary");
}

void tui_app_fail_closes_when_terminal_capabilities_are_blocking() {
  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "golden_ready";
  options.data_source_override =
      std::make_unique<dasall::tui::data::FakeTuiDataSource>(options.scenario_id);
  options.probe_environment = TuiTerminalProbeEnvironment{};
  options.print_final_screen = false;

  std::ostringstream captured_output;
  options.output_stream = &captured_output;

  const int exit_code = app.run(std::move(options));

  assert_equal(1, exit_code, "non-TTY startup should fail closed for the fake-only prototype");
  assert_true(!app.shutdown_clean(), "fail-closed startup should not report a clean shutdown");
  assert_true(app.startup_mode() == TuiStartupMode::FailClosed,
              "non-TTY startup should classify the terminal as fail-closed");
  assert_true(app.last_rendered_screen().empty(),
              "fail-closed startup should not emit a fake screen snapshot");
  assert_true(std::string(app.last_error()).find("TUI startup blocked") != std::string::npos,
              "fail-closed startup should surface the startup blocker message");
  assert_true(captured_output.str().find("stdin is not attached to a TTY") != std::string::npos,
              "fail-closed startup should print the terminal blocker details");
}

}  // namespace

int main() {
  try {
    tui_app_starts_full_screen_fake_session();
    tui_app_fail_closes_when_terminal_capabilities_are_blocking();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiAppStartupTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}