#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "data/FakeScenarioCatalog.h"
#include "support/TestAssertions.h"
#include "view/TuiStatusPanel.h"

#ifndef DASALL_TUI_STATUS_PANEL_HEADER
#define DASALL_TUI_STATUS_PANEL_HEADER \
  "/home/gangan/DASALL/apps/tui/src/view/TuiStatusPanel.h"
#endif

#ifndef DASALL_TUI_STATUS_PANEL_IMPL
#define DASALL_TUI_STATUS_PANEL_IMPL \
  "/home/gangan/DASALL/apps/tui/src/view/TuiStatusPanel.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeScenarioCatalog;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::view::TuiStatusPanel;
using dasall::tui::view::TuiStatusPanelRenderResult;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] const TuiStatusProjection& latest_status_projection(
    const dasall::tui::data::FakeScenario& scenario) {
  return *scenario.event_batches.back().back().status_delta;
}

[[nodiscard]] std::string join_rendered_lines(
    const TuiStatusPanelRenderResult& render_result) {
  std::string joined;
  for (const auto& line : render_result.lines) {
    if (!joined.empty()) {
      joined += '\n';
    }
    joined += line.text;
  }
  return joined;
}

void status_panel_renders_textual_badges_for_fake_status() {
  const auto scenario = FakeScenarioCatalog::load("planning_tools");
  assert_true(scenario.ok(),
              "planning_tools should provide the fake status scenario for TUI-TODO-017");

  TuiStatusPanel panel(latest_status_projection(*scenario.scenario));
  const TuiStatusPanelRenderResult render_result = panel.render_status_panel(48);
  const std::string rendered_text = join_rendered_lines(render_result);

  assert_true(!render_result.narrow_layout,
              "a regular-width fake status panel should keep the full label layout");
  assert_equal("[tool calling]",
               render_result.stage_badge,
               "stage badge should normalize the fake stage into a text badge");
  assert_equal("healthy",
               render_result.health_summary,
               "healthy fake status should stay textual even without renderer colors");
  assert_equal("running tool.search",
               render_result.decision_summary,
               "decision summary should stay explicit when a fake tool is active");
  assert_true(rendered_text.find("stage: [tool calling]") != std::string::npos,
              "status panel should render a textual stage badge line");
  assert_true(rendered_text.find("tool: tool.search") != std::string::npos,
              "status panel should render the active fake tool as text");
  assert_true(rendered_text.find("budget: Budget 64% remaining") != std::string::npos,
              "status panel should surface fake budget text directly from the projection");
  assert_true(rendered_text.find("health: healthy") != std::string::npos,
              "status panel should keep healthy state visible in text instead of color only");
  assert_true(rendered_text.find("decision: running tool.search") != std::string::npos,
              "status panel should expose an explicit decision summary line");
  assert_true(!render_result.degraded,
              "fully populated healthy fake status should not be marked degraded");
}

void status_panel_renders_pending_and_recovery_reasons() {
  const auto confirm_scenario = FakeScenarioCatalog::load("needs_confirm");
  assert_true(confirm_scenario.ok(),
              "needs_confirm should provide the pending interaction fake scenario");
  TuiStatusPanel confirm_panel(latest_status_projection(*confirm_scenario.scenario));
  const std::string confirm_text =
      join_rendered_lines(confirm_panel.render_status_panel(48));

  assert_true(confirm_text.find("pending: confirm external tool") != std::string::npos,
              "pending interaction should be rendered as normalized text");
  assert_true(confirm_text.find("decision: awaiting confirm external tool") !=
                  std::string::npos,
              "decision summary should explain waiting interaction without color-only cues");

  const auto recovering_scenario = FakeScenarioCatalog::load("recovering");
  assert_true(recovering_scenario.ok(),
              "recovering should provide the degraded fake scenario");
  TuiStatusPanel recovering_panel(latest_status_projection(*recovering_scenario.scenario));
  const TuiStatusPanelRenderResult recovering_render =
      recovering_panel.render_status_panel(48);
  const std::string recovering_text = join_rendered_lines(recovering_render);

  assert_equal("[reflecting | guarded]",
               recovering_render.stage_badge,
               "guarded safe mode should be visible inside the stage badge text");
  assert_true(recovering_render.health_summary.find("degraded: degraded") !=
                  std::string::npos &&
                  recovering_render.health_summary.find("guarded") != std::string::npos,
              "health summary should expose degraded and guarded text together");
  assert_true(recovering_text.find(
                  "recovery: Accepted safe replay window after tool timeout.") !=
                  std::string::npos,
              "recovery summary should be rendered directly from the fake projection");
  assert_true(recovering_text.find(
                  "decision: Accepted safe replay window after tool timeout.") !=
                  std::string::npos,
              "decision summary should fall back to the recovery summary when present");
  assert_true(recovering_render.degraded,
              "degraded fake health state should mark the whole status panel degraded");
}

void status_panel_fails_closed_for_missing_fields_in_narrow_layout() {
  TuiStatusPanel panel(TuiStatusProjection{});
  const TuiStatusPanelRenderResult render_result = panel.render_status_panel(20);
  const std::string rendered_text = join_rendered_lines(render_result);

  assert_true(render_result.narrow_layout,
              "narrow panel widths should switch the status panel into compact labels");
  assert_equal(8,
               static_cast<int>(render_result.lines.size()),
               "status panel should always render the full eight textual rows");
  assert_equal("[unknown stage]",
               render_result.stage_badge,
               "missing stage should fail closed with an explicit unknown badge");
  assert_equal("degraded: unknown health",
               render_result.health_summary,
               "missing health should fail closed with explicit degraded text");
  assert_equal("degraded: decision summary unavailable",
               render_result.decision_summary,
               "missing status projection should not leave the decision summary blank");
  assert_true(rendered_text.find("stg: [unknown stage]") != std::string::npos,
              "compact layout should abbreviate labels while preserving text meaning");
  assert_true(rendered_text.find("bdgt: degraded: unknown budget") != std::string::npos,
              "missing budget should render an explicit degraded fallback");
  assert_true(rendered_text.find("safe: degraded: unknown safe mode") !=
                  std::string::npos,
              "missing safe mode should render an explicit degraded fallback");
  assert_true(render_result.degraded,
              "missing critical status fields should mark the panel degraded");
}

void status_panel_files_avoid_owner_private_or_renderer_dependencies() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_STATUS_PANEL_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_STATUS_PANEL_IMPL});

  for (const std::string* file_text : {&header_text, &impl_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "status panel files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "status panel files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "status panel files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "status panel files should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "status panel files should stay independent from the renderer implementation");
    assert_true(file_text->find("iostream") == std::string::npos,
                "status panel production files should not perform stream I/O");
    assert_true(file_text->find("fstream") == std::string::npos,
                "status panel production files should not depend on file I/O");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "status panel production files should not depend on filesystem APIs");
    assert_true(file_text->find("chrono") == std::string::npos,
                "status panel production files should not read clock state directly");
  }
}

}  // namespace

int main() {
  try {
    status_panel_renders_textual_badges_for_fake_status();
    status_panel_renders_pending_and_recovery_reasons();
    status_panel_fails_closed_for_missing_fields_in_narrow_layout();
    status_panel_files_avoid_owner_private_or_renderer_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiStatusPanelTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}