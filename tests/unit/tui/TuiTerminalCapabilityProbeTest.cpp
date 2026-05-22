#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "support/TestAssertions.h"
#include "terminal/TuiTerminalCapabilityProbe.h"

#ifndef DASALL_TUI_TERMINAL_CAPABILITY_HEADER
#define DASALL_TUI_TERMINAL_CAPABILITY_HEADER \
  "/home/gangan/DASALL/apps/tui/src/terminal/TuiTerminalCapabilityProbe.h"
#endif

#ifndef DASALL_TUI_TERMINAL_CAPABILITY_IMPL
#define DASALL_TUI_TERMINAL_CAPABILITY_IMPL \
  "/home/gangan/DASALL/apps/tui/src/terminal/TuiTerminalCapabilityProbe.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::terminal::TuiStartupMode;
using dasall::tui::terminal::TuiTerminalCapabilities;
using dasall::tui::terminal::TuiTerminalCapabilityProbe;
using dasall::tui::terminal::TuiTerminalProbeEnvironment;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] bool has_issue(
    const TuiTerminalCapabilities& capabilities,
    const std::string& reason_code) {
  for (const auto& issue : capabilities.issues) {
    if (issue.reason_code == reason_code) {
      return true;
    }
  }

  return false;
}

void probe_selects_full_screen_for_large_utf8_terminal() {
  TuiTerminalCapabilityProbe probe;
  const TuiTerminalCapabilities capabilities = probe.probe(TuiTerminalProbeEnvironment{
      .stdin_is_tty = true,
      .stdout_is_tty = true,
      .stderr_is_tty = true,
      .term = "xterm-256color",
      .locale = "zh_CN.UTF-8",
      .columns = 140,
      .rows = 40,
      .utf8_enabled = true,
      .bracketed_paste_supported = true,
      .resize_events_supported = true,
      .external_editor_available = true,
  });

  assert_equal(static_cast<int>(TuiStartupMode::FullScreen),
               static_cast<int>(probe.select_startup_mode(capabilities)),
               "large UTF-8 terminals with full input capabilities should stay in full-screen mode");
  assert_true(capabilities.issues.empty(),
              "full-screen happy path should not emit startup issues");
  assert_true(probe.format_startup_error(capabilities).empty(),
              "full-screen happy path should not format a startup error");
}

void probe_selects_narrow_for_snapshot_floor_terminal() {
  TuiTerminalCapabilityProbe probe;
  const TuiTerminalCapabilities capabilities = probe.probe(TuiTerminalProbeEnvironment{
      .stdin_is_tty = true,
      .stdout_is_tty = true,
      .stderr_is_tty = true,
      .term = "screen-256color",
      .locale = "en_US.UTF-8",
      .columns = 80,
      .rows = 24,
      .utf8_enabled = true,
      .bracketed_paste_supported = true,
      .resize_events_supported = true,
      .external_editor_available = true,
  });

  assert_equal(static_cast<int>(TuiStartupMode::Narrow),
               static_cast<int>(probe.select_startup_mode(capabilities)),
               "80x24 terminals with full input capabilities should select narrow startup mode");
  assert_true(capabilities.issues.empty(),
              "narrow happy path should not emit startup issues");
}

void probe_downgrades_to_line_mode_when_advanced_input_is_missing() {
  TuiTerminalCapabilityProbe probe;
  const TuiTerminalCapabilities capabilities = probe.probe(TuiTerminalProbeEnvironment{
      .stdin_is_tty = true,
      .stdout_is_tty = true,
      .stderr_is_tty = true,
      .term = "xterm-256color",
      .locale = "C",
      .columns = 96,
      .rows = 30,
      .utf8_enabled = false,
      .bracketed_paste_supported = false,
      .resize_events_supported = false,
      .external_editor_available = true,
  });

  assert_equal(static_cast<int>(TuiStartupMode::Line),
               static_cast<int>(probe.select_startup_mode(capabilities)),
               "missing UTF-8 or advanced input support should downgrade startup to line mode");
  assert_true(has_issue(capabilities, "utf8_unavailable"),
              "line-mode downgrade should record the missing UTF-8 issue");
  assert_true(has_issue(capabilities, "bracketed_paste_unavailable"),
              "line-mode downgrade should record the missing bracketed paste issue");
  assert_true(has_issue(capabilities, "resize_unavailable"),
              "line-mode downgrade should record the missing resize issue");
  assert_true(probe.format_startup_error(capabilities).empty(),
              "line-mode downgrade should not be formatted as a startup error");
}

void probe_records_external_editor_issue_without_blocking_startup() {
  TuiTerminalCapabilityProbe probe;
  const TuiTerminalCapabilities capabilities = probe.probe(TuiTerminalProbeEnvironment{
      .stdin_is_tty = true,
      .stdout_is_tty = true,
      .stderr_is_tty = true,
      .term = "xterm-256color",
      .locale = "en_US.UTF-8",
      .columns = 88,
      .rows = 28,
      .utf8_enabled = true,
      .bracketed_paste_supported = true,
      .resize_events_supported = true,
      .external_editor_available = false,
  });

  assert_equal(static_cast<int>(TuiStartupMode::Narrow),
               static_cast<int>(probe.select_startup_mode(capabilities)),
               "missing VISUAL or EDITOR should not block startup when the terminal is otherwise healthy");
  assert_true(has_issue(capabilities, "external_editor_unavailable"),
              "external editor absence should still be surfaced as a startup issue");
  assert_true(!capabilities.has_blocking_issue(),
              "external editor absence should stay non-blocking for startup mode selection");
}

void probe_fails_closed_for_non_tty_output() {
  TuiTerminalCapabilityProbe probe;
  const TuiTerminalCapabilities capabilities = probe.probe(TuiTerminalProbeEnvironment{
      .stdin_is_tty = true,
      .stdout_is_tty = false,
      .stderr_is_tty = true,
      .term = "xterm-256color",
      .locale = "en_US.UTF-8",
      .columns = 120,
      .rows = 36,
      .utf8_enabled = true,
      .bracketed_paste_supported = true,
      .resize_events_supported = true,
      .external_editor_available = true,
  });

  const std::string startup_error = probe.format_startup_error(capabilities);

  assert_equal(static_cast<int>(TuiStartupMode::FailClosed),
               static_cast<int>(probe.select_startup_mode(capabilities)),
               "non-TTY output should fail closed instead of entering alternate-screen TUI mode");
  assert_true(has_issue(capabilities, "non_tty_stdout"),
              "non-TTY output should map to a stable startup issue code");
  assert_true(startup_error.find("Use dasall-cli") != std::string::npos,
              "non-TTY startup error should direct operators to dasall-cli");
}

void probe_fails_closed_for_invalid_term_and_tiny_terminal() {
  TuiTerminalCapabilityProbe probe;
  const TuiTerminalCapabilities capabilities = probe.probe(TuiTerminalProbeEnvironment{
      .stdin_is_tty = true,
      .stdout_is_tty = true,
      .stderr_is_tty = true,
      .term = "dumb",
      .locale = "en_US.UTF-8",
      .columns = 20,
      .rows = 8,
      .utf8_enabled = true,
      .bracketed_paste_supported = false,
      .resize_events_supported = false,
      .external_editor_available = false,
  });

  const std::string startup_error = probe.format_startup_error(capabilities);

  assert_equal(static_cast<int>(TuiStartupMode::FailClosed),
               static_cast<int>(probe.select_startup_mode(capabilities)),
               "unsupported TERM or a terminal smaller than the line-mode floor should fail closed");
  assert_true(has_issue(capabilities, "invalid_term"),
              "invalid TERM should map to a stable startup issue code");
  assert_true(has_issue(capabilities, "terminal_too_small"),
              "tiny terminals should map to a stable startup issue code");
  assert_true(startup_error.find("TERM is empty, dumb, or unsupported") != std::string::npos,
              "invalid TERM should be included in the formatted startup error");
  assert_true(startup_error.find("below the 40x12 minimum") != std::string::npos,
              "tiny terminals should be included in the formatted startup error");
}

void terminal_probe_files_avoid_owner_private_or_renderer_dependencies() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_TERMINAL_CAPABILITY_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_TERMINAL_CAPABILITY_IMPL});

  for (const std::string* file_text : {&header_text, &impl_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "terminal probe files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "terminal probe files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "terminal probe files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "terminal probe files should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "terminal probe files should remain independent from the renderer implementation");
    assert_true(file_text->find("iostream") == std::string::npos,
                "terminal probe production files should not perform stream I/O");
    assert_true(file_text->find("fstream") == std::string::npos,
                "terminal probe production files should not depend on file I/O");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "terminal probe production files should not depend on filesystem APIs");
    assert_true(file_text->find("chrono") == std::string::npos,
                "terminal probe production files should not read clock state directly");
  }
}

}  // namespace

int main() {
  try {
    probe_selects_full_screen_for_large_utf8_terminal();
    probe_selects_narrow_for_snapshot_floor_terminal();
    probe_downgrades_to_line_mode_when_advanced_input_is_missing();
    probe_records_external_editor_issue_without_blocking_startup();
    probe_fails_closed_for_non_tty_output();
    probe_fails_closed_for_invalid_term_and_tiny_terminal();
    terminal_probe_files_avoid_owner_private_or_renderer_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiTerminalCapabilityProbeTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}