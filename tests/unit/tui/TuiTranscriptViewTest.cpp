#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "support/TestAssertions.h"
#include "view/TuiTranscriptView.h"

#ifndef DASALL_TUI_TRANSCRIPT_VIEW_HEADER
#define DASALL_TUI_TRANSCRIPT_VIEW_HEADER \
  "/home/gangan/DASALL/apps/tui/src/view/TuiTranscriptView.h"
#endif

#ifndef DASALL_TUI_TRANSCRIPT_VIEW_IMPL
#define DASALL_TUI_TRANSCRIPT_VIEW_IMPL \
  "/home/gangan/DASALL/apps/tui/src/view/TuiTranscriptView.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::model::TuiMessageView;
using dasall::tui::view::TuiTranscriptRenderResult;
using dasall::tui::view::TuiTranscriptView;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string join_visible_lines(const TuiTranscriptRenderResult& render_result) {
  std::string joined;
  for (const auto& line : render_result.visible_lines) {
    if (!joined.empty()) {
      joined += '\n';
    }
    joined += line.text;
  }
  return joined;
}

void transcript_view_renders_controlled_summary_only() {
  std::vector<TuiMessageView> transcript = {
      TuiMessageView{.role = "user",
                     .content = "Summarized request for the current foreground session.",
                     .timestamp = "2026-05-22T12:10:00Z",
                     .badges = {"turn"}},
      TuiMessageView{.role = "assistant",
                     .content = "Plan drafted and tool execution scheduled.",
                     .timestamp = "2026-05-22T12:10:02Z",
                     .badges = {"accepted_async"}},
      TuiMessageView{.role = "tool",
                     .content =
                         "reasoning_content: internal chain-of-thought with sk-test-secret raw tool output",
                     .timestamp = "2026-05-22T12:10:03Z",
                     .badges = {"tool.search", "planning"},
                     .collapsible = true,
                     .collapsed = true},
  };

  TuiTranscriptView view(std::move(transcript));
  const TuiTranscriptRenderResult render_result = view.render_transcript(8, 96);
  const std::string rendered_text = join_visible_lines(render_result);

  assert_true(rendered_text.find("Summarized request for the current foreground session.") !=
                  std::string::npos,
              "transcript view should render the visible user summary line");
  assert_true(rendered_text.find("Plan drafted and tool execution scheduled.") !=
                  std::string::npos,
              "transcript view should render the visible assistant summary line");
  assert_true(rendered_text.find("[redacted unsafe transcript summary]") != std::string::npos,
              "transcript view should fail-closed when unsafe transcript content reaches the view");
  assert_true(rendered_text.find("reasoning_content") == std::string::npos,
              "transcript view should not leak provider-private reasoning markers");
  assert_true(rendered_text.find("chain-of-thought") == std::string::npos,
              "transcript view should not leak raw chain-of-thought markers");
  assert_true(rendered_text.find("sk-test-secret") == std::string::npos,
              "transcript view should not leak secret-like tokens into the visible transcript");
  assert_true(rendered_text.find("raw tool output") == std::string::npos,
              "transcript view should not leak raw tool output into the visible transcript");
}

void transcript_view_toggles_collapsible_rows_only() {
  std::vector<TuiMessageView> transcript = {
      TuiMessageView{.role = "tool",
                     .content =
                         "Collected three planning references and condensed them into a bounded observation summary.",
                     .timestamp = "2026-05-22T12:11:00Z",
                     .badges = {"tool.search", "planning"},
                     .collapsible = true,
                     .collapsed = true},
      TuiMessageView{.role = "system",
                     .content = "Foreground session remains healthy.",
                     .timestamp = "2026-05-22T12:11:01Z",
                     .badges = {"status.updated"}},
  };

  TuiTranscriptView view(std::move(transcript));
  const std::string collapsed_text = join_visible_lines(view.render_transcript(8, 96));
  assert_true(collapsed_text.find("[collapsed]") != std::string::npos,
              "collapsed transcript rows should expose an explicit collapsed marker");

  assert_true(view.toggle_collapse(0),
              "toggle_collapse should expand a valid collapsible transcript row");
  const std::string expanded_text = join_visible_lines(view.render_transcript(8, 96));
  assert_true(expanded_text.find("[collapsed]") == std::string::npos,
              "expanded transcript rows should remove the collapsed marker");
  assert_true(expanded_text.find("bounded observation summary.") != std::string::npos,
              "expanded transcript rows should render the full safe tool summary");

  assert_true(!view.toggle_collapse(1),
              "toggle_collapse should fail-closed for non-collapsible transcript rows");
  assert_true(!view.toggle_collapse(99),
              "toggle_collapse should fail-closed for out-of-range transcript indices");
}

void transcript_view_scrolls_to_bottom_for_latest_lines() {
  std::vector<TuiMessageView> transcript;
  transcript.push_back(TuiMessageView{.role = "user",
                                      .content = "message one",
                                      .timestamp = "2026-05-22T12:12:00Z",
                                      .badges = {}});
  transcript.push_back(TuiMessageView{.role = "assistant",
                                      .content = "message two",
                                      .timestamp = "2026-05-22T12:12:01Z",
                                      .badges = {}});
  transcript.push_back(TuiMessageView{.role = "tool",
                                      .content = "message three",
                                      .timestamp = "2026-05-22T12:12:02Z",
                                      .badges = {}});
  transcript.push_back(TuiMessageView{.role = "system",
                                      .content = "message four",
                                      .timestamp = "2026-05-22T12:12:03Z",
                                      .badges = {}});

  TuiTranscriptView view(std::move(transcript));
  view.scroll_to_bottom(4, 64);

  const TuiTranscriptRenderResult render_result = view.render_transcript(4, 64);
  const std::string rendered_text = join_visible_lines(render_result);

  assert_equal(8,
               static_cast<int>(render_result.total_line_count),
               "four short transcript rows should flatten into four headers and four body lines");
  assert_equal(4,
               static_cast<int>(render_result.scroll_offset),
               "scroll_to_bottom should move the viewport to the last four flattened transcript lines");
  assert_true(!render_result.at_top,
              "scroll_to_bottom should move the transcript away from the top when overflow exists");
  assert_true(render_result.at_bottom,
              "scroll_to_bottom should keep the transcript anchored at the latest visible lines");
  assert_true(rendered_text.find("message one") == std::string::npos,
              "bottom-anchored rendering should drop the earliest lines when the viewport overflows");
  assert_true(rendered_text.find("message three") != std::string::npos &&
                  rendered_text.find("message four") != std::string::npos,
              "bottom-anchored rendering should preserve the latest transcript summaries");
}

void transcript_view_honors_manual_scroll_offset() {
  std::vector<TuiMessageView> transcript;
  for (int index = 0; index < 6; ++index) {
    transcript.push_back(TuiMessageView{.role = index % 2 == 0 ? "user" : "assistant",
                                        .content = "message " + std::to_string(index),
                                        .timestamp = "2026-05-25T10:00:00Z",
                                        .badges = {}});
  }

  TuiTranscriptView view(std::move(transcript));
  view.set_scroll_offset(2);

  const TuiTranscriptRenderResult render_result = view.render_transcript(4, 64);
  const std::string rendered_text = join_visible_lines(render_result);

  assert_equal(2,
               static_cast<int>(render_result.scroll_offset),
               "manual transcript scroll offset should be preserved before viewport clamping");
  assert_true(rendered_text.find("message 0") == std::string::npos,
              "manual transcript scroll should hide rows above the selected offset");
  assert_true(rendered_text.find("message 1") != std::string::npos,
              "manual transcript scroll should render rows starting at the selected offset");
}

void transcript_view_files_avoid_owner_private_includes_and_renderer_io() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_TRANSCRIPT_VIEW_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_TRANSCRIPT_VIEW_IMPL});

  for (const std::string* file_text : {&header_text, &impl_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "transcript view files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "transcript view files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "transcript view files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "transcript view files should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "transcript view files should stay independent from the renderer implementation");
    assert_true(file_text->find("iostream") == std::string::npos,
                "transcript view production files should not perform stream I/O");
    assert_true(file_text->find("fstream") == std::string::npos,
                "transcript view production files should not depend on file I/O");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "transcript view production files should not depend on filesystem APIs");
    assert_true(file_text->find("chrono") == std::string::npos,
                "transcript view production files should not read clock state directly");
  }
}

}  // namespace

int main() {
  try {
    transcript_view_renders_controlled_summary_only();
    transcript_view_toggles_collapsible_rows_only();
    transcript_view_scrolls_to_bottom_for_latest_lines();
    transcript_view_honors_manual_scroll_offset();
    transcript_view_files_avoid_owner_private_includes_and_renderer_io();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiTranscriptViewTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}