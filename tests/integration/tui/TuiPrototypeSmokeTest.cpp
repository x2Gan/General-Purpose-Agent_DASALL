#include <exception>
#include <iostream>
#include <string>

#include "app/TuiApp.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::app::TuiApp;
using dasall::tui::app::TuiAppOptions;
using dasall::tui::data::TuiRoutePreferenceMode;
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

void tui_prototype_smoke_renders_fake_transcript_status_selector_and_composer() {
  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "planning_tools";
  options.probe_environment = make_full_screen_environment();
  options.bootstrap_tick_count = 2;
  options.initial_draft = "Hold the current draft while tool.search is running.";
  options.selector_preview_mode = TuiRoutePreferenceMode::PinModel;
  options.print_final_screen = false;

  const int exit_code = app.run(std::move(options));
  const std::string& screen = app.last_rendered_screen();
  std::string all_frames;
  for (const auto& frame : app.rendered_frames()) {
    all_frames += frame;
    all_frames.push_back('\n');
  }

  assert_equal(0, exit_code, "prototype smoke should exit cleanly for the planning_tools fake scenario");
  assert_true(app.shutdown_clean(), "prototype smoke should close the fake session cleanly");
  assert_true(!screen.empty(), "prototype smoke should render a deterministic terminal screen");
  assert_true(app.rendered_frames().size() >= 3,
              "prototype smoke should capture startup, tick, and final frames");
  assert_true(all_frames.find("Plan drafted and tool execution scheduled.") != std::string::npos,
              "prototype smoke should surface the fake transcript receipt summary in the captured frames");
  assert_true(all_frames.find("Collected three planning references for the next turn.") != std::string::npos,
              "prototype smoke should surface the fake tool summary transcript line in the captured frames");
  assert_true(screen.find("tool: tool.search") != std::string::npos,
              "prototype smoke should surface the current tool in the status panel");
  assert_true(screen.find("mode=pending-interaction submit=disabled dirty=yes") != std::string::npos,
              "prototype smoke should keep the composer locked while fake tool activity is pending");
  assert_true(screen.find("[NEXT TURN PREFERENCE]") != std::string::npos,
              "prototype smoke should render the selector preview modal overlay");
  assert_true(screen.find("kind=selector") != std::string::npos,
              "prototype smoke should label the selector modal kind in the overlay body");
}

}  // namespace

int main() {
  try {
    tui_prototype_smoke_renders_fake_transcript_status_selector_and_composer();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiPrototypeSmokeTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}