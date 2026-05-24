#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data/FakeScenarioCatalog.h"
#include "support/TestAssertions.h"
#include "terminal/FtxuiRendererAdapter.h"
#include "view/TuiTextWidth.h"

#ifndef DASALL_TUI_RENDERER_ADAPTER_HEADER
#define DASALL_TUI_RENDERER_ADAPTER_HEADER \
  "/home/gangan/DASALL/apps/tui/src/terminal/FtxuiRendererAdapter.h"
#endif

#ifndef DASALL_TUI_RENDERER_ADAPTER_IMPL
#define DASALL_TUI_RENDERER_ADAPTER_IMPL \
  "/home/gangan/DASALL/apps/tui/src/terminal/FtxuiRendererAdapter.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeScenario;
using dasall::tui::data::FakeScenarioCatalog;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::model::TuiBanner;
using dasall::tui::model::TuiBannerLevel;
using dasall::tui::model::TuiComposerState;
using dasall::tui::model::TuiFocusState;
using dasall::tui::model::TuiMessageView;
using dasall::tui::model::TuiModalKind;
using dasall::tui::model::TuiModalState;
using dasall::tui::model::TuiScreenModel;
using dasall::tui::terminal::FtxuiRendererAdapter;
using dasall::tui::view::terminal_display_width;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      lines.emplace_back(text.substr(start));
      break;
    }
    lines.emplace_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

[[nodiscard]] std::size_t find_line_containing(const std::vector<std::string>& lines,
                                               std::string_view needle) {
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (lines[index].find(needle) != std::string::npos) {
      return index;
    }
  }
  return lines.size();
}

[[nodiscard]] std::optional<TuiStatusProjection> latest_status_projection(
    const FakeScenario& scenario) {
  if (scenario.event_batches.empty() || scenario.event_batches.back().empty()) {
    return std::nullopt;
  }

  return scenario.event_batches.back().back().status_delta;
}

[[nodiscard]] TuiScreenModel make_screen_model(
    const FakeScenario& scenario,
    std::vector<TuiMessageView> transcript,
    TuiComposerState composer,
    TuiFocusState focus,
    std::vector<TuiBanner> banners = {},
    TuiModalState modal = {}) {
  TuiScreenModel model;
  model.session = scenario.session;
  model.transcript = std::move(transcript);
  model.status = latest_status_projection(scenario).value_or(TuiStatusProjection{
      .stage = "ready",
      .current_tool = "",
      .pending_interaction = "",
      .budget_summary = "Budget 100% remaining",
      .recovery_summary = "",
      .health_summary = "healthy",
      .safe_mode_summary = "normal",
  });
  model.route = scenario.route_catalog.current_route;
  model.composer = std::move(composer);
  model.focus = focus;
  model.banners = std::move(banners);
  model.modal = std::move(modal);
  model.debug_reason = scenario.scenario_id;
  return model;
}

void main_layout_snapshot_renders_full_screen_ready_shell() {
  const auto loaded = FakeScenarioCatalog::load("golden_ready");
  assert_true(loaded.ok(), "golden_ready should load for the full-screen snapshot baseline");

  const TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      {TuiMessageView{.role = "user",
                      .content = "Summarize the current session state.",
                      .timestamp = "2026-05-22T10:00:01Z",
                      .badges = {"turn"}},
       TuiMessageView{.role = "assistant",
                      .content = "Session is ready. No daemon or tool interaction is pending.",
                      .timestamp = "2026-05-22T10:00:02Z",
                      .badges = {"ready"}}},
      TuiComposerState{.text = "",
               .mode = "ready",
               .history_query = std::nullopt,
               .can_submit = true,
               .dirty = false,
               .cursor_visible = true,
               .activity_indicator = {}},
      TuiFocusState::Composer);

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 120, 36);
  const auto lines = split_lines(screen);

  assert_equal(36,
               static_cast<int>(lines.size()),
               "120x36 snapshot should render exactly 36 terminal rows");
  for (const auto& line : lines) {
    assert_equal(120,
                 static_cast<int>(terminal_display_width(line)),
                 "120x36 snapshot should keep every rendered row terminal-width-stable");
  }

  assert_true(screen.find("SESSION fake-golden-ready-001") != std::string::npos,
              "full-screen snapshot should surface the session banner in the header");
  assert_true(screen.find("ROUTE provider-openai/gpt-4.1 [balanced] | NEXT auto") !=
                  std::string::npos,
              "full-screen snapshot should surface the current route and next-turn preference");
  assert_true(find_line_containing(lines, "[TRANSCRIPT]") < lines.size() &&
                  find_line_containing(lines, "[STATUS]") ==
                      find_line_containing(lines, "[TRANSCRIPT]"),
              "full-screen snapshot should keep transcript and status boxes on the same row");
  assert_true(screen.find("[empty transcript]") == std::string::npos,
              "full-screen snapshot should render the provided transcript summaries");
}

void main_layout_snapshot_renders_narrow_cjk_without_side_overlap() {
  const auto loaded = FakeScenarioCatalog::load("narrow_cjk");
  assert_true(loaded.ok(), "narrow_cjk should load for the 80x24 snapshot baseline");

  const TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      {TuiMessageView{.role = "user",
                      .content = "请总结当前状态。",
                      .timestamp = "2026-05-22T10:05:01Z",
                      .badges = {"cjk"}},
       TuiMessageView{.role = "assistant",
                      .content = "中文上下文片段已收集，可用于窄屏回放。",
                      .timestamp = "2026-05-22T10:05:02Z",
                      .badges = {"narrow", "summary"}}},
      TuiComposerState{.text = "请总结最新状态",
                       .mode = "editing",
                       .history_query = std::nullopt,
                       .can_submit = true,
                       .dirty = true,
                       .cursor_visible = true,
                       .activity_indicator = {}},
      TuiFocusState::Composer);

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 80, 24);
  const auto lines = split_lines(screen);

  assert_equal(24,
               static_cast<int>(lines.size()),
               "80x24 snapshot should render exactly 24 terminal rows");
  for (const auto& line : lines) {
    assert_equal(80,
                 static_cast<int>(terminal_display_width(line)),
                 "80x24 snapshot should keep every rendered row terminal-width-stable");
  }

  const std::size_t transcript_row = find_line_containing(lines, "[TRANSCRIPT]");
  const std::size_t status_row = find_line_containing(lines, "[STATUS]");
  assert_true(transcript_row < status_row,
              "narrow snapshot should stack the status panel below the transcript instead of side-splitting");
  assert_true(screen.find("请总结当前状态。") != std::string::npos,
              "narrow snapshot should preserve the visible CJK user summary");
  assert_true(screen.find("中文上下文片段已收集，可用于窄屏回放。") != std::string::npos,
              "narrow snapshot should preserve the visible CJK assistant summary");
  assert_true(screen.find("预算剩余 66%") != std::string::npos,
              "narrow snapshot should keep the CJK budget summary visible in the stacked status panel");
}

void main_layout_snapshot_keeps_cjk_rows_aligned_with_status_panel() {
  const auto loaded = FakeScenarioCatalog::load("golden_ready");
  assert_true(loaded.ok(), "golden_ready should load for the manual-terminal CJK alignment baseline");

  TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      {TuiMessageView{.role = "system",
                      .content = "BLK-TUI-006 manual terminal ready. Enter submits, Ctrl-J inserts a newline, "
                                 "Up/Down recall history, Ctrl-R reverse-searches history, /editor opens "
                                 "VISUAL/EDITOR, /status and /session open checks, /exit quits.",
                      .timestamp = "2026-05-24T21:09:55",
                      .badges = {"manual", "composer"}},
       TuiMessageView{.role = "system",
                      .content = "CJK sample: 中文输入、かな、한글、emoji-less UTF-8 text should stay "
                                 "readable during IME commit and live resize.",
                      .timestamp = "2026-05-24T21:09:55",
                      .badges = {"cjk", "ime", "resize"}}},
      TuiComposerState{.text = "",
                       .mode = "ready",
                       .history_query = std::nullopt,
                       .can_submit = true,
                       .dirty = false,
                       .cursor_visible = true,
                       .activity_indicator = {}},
      TuiFocusState::Composer,
      {TuiBanner{.level = TuiBannerLevel::Warning,
                 .title = "Terminal degraded",
                 .message = "VISUAL and EDITOR are unset; /editor will remain disabled.",
                 .reason_code = "editor_unset",
                 .sticky = true}});
  model.session.session_id = "manual-terminal-blk-tui-006";
  model.session.profile_id = "local-manual";
  model.session.startup_mode = "full";
  model.route.current_provider_id = "manual";
  model.route.current_model_id = "tui-terminal";
  model.route.current_depth_tier = "local";
  model.status.stage = "ready";
  model.status.budget_summary = "Manual evidence run";
  model.status.health_summary = "degraded: terminal active";
  model.status.safe_mode_summary = "full 119x50";

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 119, 50);
  const auto lines = split_lines(screen);

  assert_equal(50,
               static_cast<int>(lines.size()),
               "119x50 manual-terminal snapshot should render exactly 50 terminal rows");
  for (const auto& line : lines) {
    assert_equal(119,
                 static_cast<int>(terminal_display_width(line)),
                 "119x50 manual-terminal snapshot should keep every row terminal-width-stable");
  }

  const std::size_t cjk_row = find_line_containing(lines, "CJK sample:");
  assert_true(cjk_row < lines.size(),
              "manual-terminal snapshot should include the CJK sample row");
  assert_true(find_line_containing(lines, "safe mode: full 119x50") < lines.size(),
              "manual-terminal snapshot should keep the safe-mode row visible in the status panel");
  assert_true(lines[cjk_row].size() > terminal_display_width(lines[cjk_row]),
              "CJK alignment test should exercise multi-byte text rather than ASCII-only rows");
}

void main_layout_snapshot_renders_selector_modal_overlay() {
  const auto loaded = FakeScenarioCatalog::load("route_switch");
  assert_true(loaded.ok(), "route_switch should load for the selector modal snapshot");

  TuiModalState modal;
  modal.kind = TuiModalKind::Selector;
  modal.title = "Next turn preference";
  modal.body = "Auto | Prefer depth | Pin model\nDisabled: credentials_missing, verification_pending";
  modal.actions = {"Apply", "Cancel"};
  modal.selected_action_index = 0;

  const TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      {TuiMessageView{.role = "assistant",
                      .content = "Selector preview updated with disabled reason hints.",
                      .timestamp = "2026-05-22T10:04:05Z",
                      .badges = {"selector"}}},
      TuiComposerState{.text = "",
               .mode = "ready",
               .history_query = std::nullopt,
               .can_submit = true,
               .dirty = false,
               .cursor_visible = true,
               .activity_indicator = {}},
      TuiFocusState::Modal,
      {},
      modal);

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 120, 36);
  const auto lines = split_lines(screen);

  assert_true(find_line_containing(lines, "[NEXT TURN PREFERENCE]") < lines.size(),
              "selector modal snapshot should overlay a centered modal frame");
  assert_true(screen.find("kind=selector") != std::string::npos,
              "selector modal snapshot should expose the modal kind inside the overlay body");
  assert_true(screen.find("[Apply] [Cancel]") != std::string::npos,
              "selector modal snapshot should keep the apply and cancel actions visible");
}

void main_layout_snapshot_modal_clears_underlying_history_rows() {
  const auto loaded = FakeScenarioCatalog::load("golden_ready");
  assert_true(loaded.ok(), "golden_ready should load for the modal underlay regression");

  std::vector<TuiMessageView> transcript;
  for (int index = 0; index < 24; ++index) {
    transcript.push_back(TuiMessageView{.role = index % 2 == 0 ? "user" : "assistant",
                                        .content = "UNDERLAY_SHOULD_NOT_SHOW_" +
                                                   std::to_string(index),
                                        .timestamp = "2026-05-24T21:36:00",
                                        .badges = {"history"}});
  }

  TuiModalState modal;
  modal.kind = TuiModalKind::Help;
  modal.title = "Manual terminal help";
  modal.body = "Short help body.";
  modal.actions = {"Close"};
  modal.selected_action_index = 0;

  const TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      std::move(transcript),
      TuiComposerState{.text = "",
                       .mode = "ready",
                       .history_query = std::nullopt,
                       .can_submit = true,
                       .dirty = false,
                       .cursor_visible = true,
                       .activity_indicator = {}},
      TuiFocusState::Modal,
      {},
      modal);

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 140, 56);
  const auto lines = split_lines(screen);
  const auto metrics = renderer.apply_layout_metrics(140, 56);
  const std::size_t modal_x = (metrics.terminal_width - metrics.modal.width) / 2U;
  const std::size_t modal_y = (metrics.terminal_height - metrics.modal.height) / 2U;

  assert_true(find_line_containing(lines, "[NEXT TURN PREFERENCE]") < lines.size(),
              "help modal should render above the long transcript history");

  constexpr std::size_t kModalContentLineCount = 3U;
  for (std::size_t row = modal_y + 1U + kModalContentLineCount;
       row + 1U < modal_y + metrics.modal.height;
       ++row) {
    const std::string modal_interior = lines[row].substr(modal_x + 1U,
                                                        metrics.modal.width - 2U);
    assert_true(modal_interior.find("UNDERLAY_SHOULD_NOT_SHOW") == std::string::npos,
                "modal blank rows should clear underlying transcript history");
  }
}

void main_layout_snapshot_renders_busy_draft_banner() {
  const auto loaded = FakeScenarioCatalog::load("planning_tools");
  assert_true(loaded.ok(), "planning_tools should load for the busy-draft snapshot");

  std::vector<TuiBanner> banners;
  banners.push_back(TuiBanner{.level = TuiBannerLevel::Warning,
                              .title = "Busy draft",
                              .message = "Busy draft is locked while tool execution is in progress.",
                              .reason_code = std::string{"busy_draft"},
                              .sticky = true});

  const TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      {TuiMessageView{.role = "user",
                      .content = "Draft a plan for the next action.",
                      .timestamp = "2026-05-22T10:01:01Z",
                      .badges = {"turn"}},
       TuiMessageView{.role = "assistant",
                      .content = "Plan drafted and tool execution scheduled.",
                      .timestamp = "2026-05-22T10:01:03Z",
                      .badges = {"accepted_async"}}},
      TuiComposerState{.text = "Hold the current draft while tool.search is running.",
                       .mode = "pending-interaction",
                       .history_query = std::nullopt,
                       .can_submit = false,
                       .dirty = true,
                       .cursor_visible = true,
                       .activity_indicator = {}},
      TuiFocusState::Composer,
      banners);

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 120, 36);

  assert_true(screen.find("WARN Busy draft | Busy draft is locked while tool execution is in progress. | sticky") !=
                  std::string::npos,
              "busy-draft snapshot should surface the sticky warning banner in the header");
  assert_true(screen.find("tool: tool.search") != std::string::npos,
              "busy-draft snapshot should keep the active tool visible in the status panel");
  assert_true(screen.find("mode=pending-interaction submit=disabled dirty=yes") !=
                  std::string::npos,
              "busy-draft snapshot should expose the locked composer state inside the composer panel");
}

void main_layout_snapshot_renders_cursor_and_waiting_spinner() {
  const auto loaded = FakeScenarioCatalog::load("planning_tools");
  assert_true(loaded.ok(), "planning_tools should load for the composer spinner snapshot");

  const TuiScreenModel model = make_screen_model(
      *loaded.scenario,
      {TuiMessageView{.role = "user",
                      .content = "Wait for the model result.",
                      .timestamp = "2026-05-24T21:42:00",
                      .badges = {"submitted"}}},
      TuiComposerState{.text = "",
                       .mode = "pending-interaction",
                       .history_query = std::nullopt,
                       .can_submit = false,
                       .dirty = false,
                       .cursor_visible = true,
                       .activity_indicator = "processing.."},
      TuiFocusState::Composer);

  const FtxuiRendererAdapter renderer;
  const std::string screen = renderer.render_to_screen(model, 120, 36);

  assert_true(screen.find("processing..") != std::string::npos,
              "composer should render the dots spinner in the input line after submit");
  assert_true(screen.find("[draft empty]|") == std::string::npos,
              "composer should hide the input cursor while the system is processing a submit");
  assert_true(screen.find("mode=pending-interaction submit=disabled dirty=no") !=
                  std::string::npos &&
                  screen.find("wait=") == std::string::npos,
              "composer should keep pending state metadata separate from the input-line spinner");
}

void renderer_files_avoid_owner_private_dependencies() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_RENDERER_ADAPTER_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_RENDERER_ADAPTER_IMPL});

  for (const std::string* file_text : {&header_text, &impl_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "renderer adapter files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "renderer adapter files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "renderer adapter files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "renderer adapter files should not include profile private headers");
  }

  assert_true(header_text.find("ftxui") == std::string::npos,
              "renderer adapter header should not leak FTXUI types across the apps/tui boundary");
  assert_true(impl_text.find("filesystem") == std::string::npos,
              "renderer adapter implementation should not depend on filesystem APIs");
  assert_true(impl_text.find("iostream") == std::string::npos,
              "renderer adapter implementation should not perform stream I/O");
}

}  // namespace

int main() {
  try {
    main_layout_snapshot_renders_full_screen_ready_shell();
    main_layout_snapshot_renders_narrow_cjk_without_side_overlap();
    main_layout_snapshot_keeps_cjk_rows_aligned_with_status_panel();
    main_layout_snapshot_renders_selector_modal_overlay();
    main_layout_snapshot_modal_clears_underlying_history_rows();
    main_layout_snapshot_renders_busy_draft_banner();
    main_layout_snapshot_renders_cursor_and_waiting_spinner();
    renderer_files_avoid_owner_private_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiMainLayoutSnapshotTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}