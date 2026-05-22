#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "support/TestAssertions.h"
#include "view/TuiDesignTokens.h"
#include "view/TuiLayoutMetrics.h"

#ifndef DASALL_TUI_DESIGN_TOKENS_HEADER
#define DASALL_TUI_DESIGN_TOKENS_HEADER \
  "/home/gangan/DASALL/apps/tui/src/view/TuiDesignTokens.h"
#endif

#ifndef DASALL_TUI_LAYOUT_METRICS_HEADER
#define DASALL_TUI_LAYOUT_METRICS_HEADER \
  "/home/gangan/DASALL/apps/tui/src/view/TuiLayoutMetrics.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::view::TuiDesignTokens;
using dasall::tui::view::TuiLayoutMetrics;
using dasall::tui::view::TuiLayoutMode;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void design_tokens_freeze_breakpoints_and_spacing_defaults() {
  constexpr TuiDesignTokens tokens = TuiDesignTokens::defaults();

  assert_equal(40,
               static_cast<int>(tokens.breakpoints.minimum_width),
               "design tokens should freeze the minimum width floor for line mode");
  assert_equal(12,
               static_cast<int>(tokens.breakpoints.minimum_height),
               "design tokens should freeze the minimum height floor for line mode");
  assert_equal(80,
               static_cast<int>(tokens.breakpoints.narrow_width),
               "design tokens should freeze the narrow snapshot width baseline");
  assert_equal(24,
               static_cast<int>(tokens.breakpoints.narrow_height),
               "design tokens should freeze the narrow snapshot height baseline");
  assert_equal(120,
               static_cast<int>(tokens.breakpoints.full_width),
               "design tokens should freeze the full snapshot width baseline");
  assert_equal(36,
               static_cast<int>(tokens.breakpoints.full_height),
               "design tokens should freeze the full snapshot height baseline");
  assert_true(tokens.spacing.composer_max_lines > tokens.spacing.composer_min_lines,
              "design tokens should allow the full-screen composer to grow beyond the minimum");
  assert_equal(2,
               static_cast<int>(tokens.spacing.transcript_indent),
               "design tokens should keep transcript indentation aligned with the textual baseline");
  assert_equal(34,
               static_cast<int>(tokens.breakpoints.full_status_panel_width),
               "design tokens should freeze a deterministic full-screen status column width");
  assert_true(tokens.canvas.red != tokens.surface.red ||
                  tokens.canvas.green != tokens.surface.green ||
                  tokens.canvas.blue != tokens.surface.blue,
              "design tokens should distinguish canvas and surface colors instead of collapsing them");
}

void layout_metrics_switch_between_full_narrow_and_line_modes() {
  constexpr TuiLayoutMetrics full = TuiLayoutMetrics::for_terminal(120, 36);
  constexpr TuiLayoutMetrics narrow = TuiLayoutMetrics::for_terminal(80, 24);
  constexpr TuiLayoutMetrics line = TuiLayoutMetrics::for_terminal(39, 11);

  assert_true(full.mode == TuiLayoutMode::FullScreen,
              "120x36 should select the full-screen renderer layout mode");
  assert_true(full.show_status_panel && !full.status_panel_stacked,
              "full-screen layout should keep the status panel in a dedicated side column");
  assert_true(full.transcript.width > full.status_panel.width,
              "full-screen layout should keep the transcript as the dominant region");
  assert_true(full.transcript.height == full.status_panel.height,
              "full-screen layout should align transcript and status column heights");
  assert_true(full.modal.visible && full.modal.width > 0 && full.modal.height > 0,
              "full-screen layout should reserve a stable modal overlay footprint");

  assert_true(narrow.mode == TuiLayoutMode::Narrow,
              "80x24 should select the narrow snapshot layout mode");
  assert_true(narrow.show_status_panel && narrow.status_panel_stacked,
              "narrow layout should stack the status panel below the transcript to avoid overlap");
  assert_true(narrow.transcript.width == narrow.status_panel.width,
              "stacked narrow layout should keep transcript and status panel widths aligned");
  assert_true(narrow.transcript.height >= 8,
              "narrow layout should preserve at least eight transcript rows for CJK snapshots");
  assert_true(narrow.modal.visible,
              "narrow layout should still expose modal overlay metrics for selector snapshots");

  assert_true(line.mode == TuiLayoutMode::Line,
              "sub-floor terminals should fail closed into line layout metrics");
  assert_true(!line.show_status_panel && !line.status_panel.visible,
              "line layout should drop the status panel instead of forcing an overlapping split");
  assert_true(!line.show_selector_strip,
              "line layout should drop the selector strip on cramped terminals");
  assert_true(!line.modal.visible,
              "line layout should not reserve modal overlay space below the minimum terminal floor");
}

void design_token_headers_avoid_renderer_or_owner_private_dependencies() {
  const std::string design_tokens_text =
      read_text_file(std::filesystem::path{DASALL_TUI_DESIGN_TOKENS_HEADER});
  const std::string layout_metrics_text =
      read_text_file(std::filesystem::path{DASALL_TUI_LAYOUT_METRICS_HEADER});

  for (const std::string* file_text : {&design_tokens_text, &layout_metrics_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "design token headers should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "design token headers should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "design token headers should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "design token headers should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "design token headers should stay renderer-agnostic before the adapter layer");
    assert_true(file_text->find("iostream") == std::string::npos,
                "design token headers should not perform stream I/O");
    assert_true(file_text->find("fstream") == std::string::npos,
                "design token headers should not depend on file I/O");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "design token headers should not depend on filesystem APIs");
    assert_true(file_text->find("chrono") == std::string::npos,
                "design token headers should not read clock state directly");
  }
}

}  // namespace

int main() {
  try {
    design_tokens_freeze_breakpoints_and_spacing_defaults();
    layout_metrics_switch_between_full_narrow_and_line_modes();
    design_token_headers_avoid_renderer_or_owner_private_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiDesignTokensTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}