#include "terminal/FtxuiRendererAdapter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>

#include "view/TuiStatusPanel.h"
#include "view/TuiTextWidth.h"
#include "view/TuiTranscriptView.h"

#if defined(DASALL_TUI_RENDERER_USE_FTXUI) && DASALL_TUI_RENDERER_USE_FTXUI
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#endif

namespace dasall::tui::terminal {
namespace {

[[nodiscard]] std::size_t clamp_nonzero(const std::size_t value) noexcept {
  return value == 0 ? 1U : value;
}

[[nodiscard]] std::size_t saturating_sub(const std::size_t left,
                                         const std::size_t right) noexcept {
  return left > right ? left - right : 0U;
}

[[nodiscard]] std::string uppercase_ascii(std::string_view text) {
  std::string uppercased;
  uppercased.reserve(text.size());
  for (const unsigned char character : text) {
    uppercased.push_back(static_cast<char>(std::toupper(character)));
  }
  return uppercased;
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  std::size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return std::string(text.substr(start, end - start));
}

[[nodiscard]] std::vector<std::string> wrap_text(std::string_view text,
                                                 const std::size_t width,
                                                 const std::size_t max_lines) {
  return view::wrap_to_terminal_width(text, width, max_lines);
}

[[nodiscard]] std::string format_focus(const model::TuiFocusState focus) {
  switch (focus) {
    case model::TuiFocusState::Transcript:
      return "transcript";
    case model::TuiFocusState::Composer:
      return "composer";
    case model::TuiFocusState::Selector:
      return "selector";
    case model::TuiFocusState::StatusPanel:
      return "status";
    case model::TuiFocusState::Modal:
      return "modal";
  }

  return "unknown";
}

[[nodiscard]] std::string format_modal_kind(const model::TuiModalKind kind) {
  switch (kind) {
    case model::TuiModalKind::None:
      return "none";
    case model::TuiModalKind::Selector:
      return "selector";
    case model::TuiModalKind::Confirm:
      return "confirm";
    case model::TuiModalKind::Help:
      return "help";
    case model::TuiModalKind::Session:
      return "session";
  }

  return "unknown";
}

[[nodiscard]] std::string format_route_summary(
    const data::TuiModelRouteProjection& route) {
  const std::string provider = trim_copy(route.current_provider_id);
  const std::string model_id = trim_copy(route.current_model_id);
  const std::string depth_tier = trim_copy(route.current_depth_tier);
  const std::string next_summary =
      trim_copy(route.next_preference.user_visible_summary).empty()
          ? std::string{"auto"}
          : trim_copy(route.next_preference.user_visible_summary);

  if (provider.empty() || model_id.empty()) {
    return "ROUTE unavailable | NEXT " + next_summary;
  }

  return "ROUTE " + provider + "/" + model_id + " [" +
         (depth_tier.empty() ? std::string{"unknown"} : depth_tier) + "] | NEXT " +
         next_summary;
}

[[nodiscard]] std::string format_banner_line(const model::TuiBanner& banner) {
  std::string level = "INFO";
  if (banner.level == model::TuiBannerLevel::Warning) {
    level = "WARN";
  } else if (banner.level == model::TuiBannerLevel::Error) {
    level = "ERR";
  }

  std::string line = level;
  if (!banner.title.empty()) {
    line += " ";
    line += banner.title;
  }
  if (!banner.message.empty()) {
    line += " | ";
    line += banner.message;
  }
  if (banner.sticky) {
    line += " | sticky";
  }
  return line;
}

[[nodiscard]] std::vector<std::string> build_header_lines(
    const model::TuiScreenModel& screen_model,
    const view::TuiLayoutMetrics& metrics) {
  std::vector<std::string> lines;
  lines.reserve(metrics.header_height);

  lines.push_back("SESSION " + trim_copy(screen_model.session.session_id) +
                  " | PROFILE " + trim_copy(screen_model.session.profile_id) +
                  " | START " + trim_copy(screen_model.session.startup_mode));
  lines.push_back(format_route_summary(screen_model.route));

  if (metrics.header_height > 2) {
    if (!screen_model.banners.empty()) {
      lines.push_back(format_banner_line(screen_model.banners.front()));
    } else {
      lines.push_back("FOCUS " + format_focus(screen_model.focus) + " | READY " +
                      trim_copy(screen_model.session.daemon_readiness));
    }
  }

  if (lines.size() > metrics.header_height) {
    lines.resize(metrics.header_height);
  }
  return lines;
}

[[nodiscard]] std::vector<std::string> build_composer_lines(
    const model::TuiComposerState& composer,
    const std::size_t width,
    const std::size_t height) {
  std::vector<std::string> lines;
  if (width == 0 || height == 0) {
    return lines;
  }

  const std::string draft = composer.text.empty() ? std::string{"[draft empty]"}
                                                  : composer.text;
  auto wrapped = wrap_text(draft, width, height);
  lines.insert(lines.end(), wrapped.begin(), wrapped.end());
  if (lines.size() < height) {
    lines.push_back("mode=" + trim_copy(composer.mode) +
                    " submit=" + (composer.can_submit ? std::string{"enabled"}
                                                       : std::string{"disabled"}) +
                    " dirty=" + (composer.dirty ? std::string{"yes"}
                                                 : std::string{"no"}));
  }
  if (lines.size() > height) {
    lines.resize(height);
  }
  return lines;
}

[[nodiscard]] std::vector<std::string> build_modal_lines(
    const model::TuiModalState& modal,
    const std::size_t width,
    const std::size_t height) {
  std::vector<std::string> lines;
  if (width == 0 || height == 0) {
    return lines;
  }

  lines.push_back("kind=" + format_modal_kind(modal.kind));
  auto body_lines = wrap_text(modal.body, width, height > 2 ? height - 2 : 1);
  lines.insert(lines.end(), body_lines.begin(), body_lines.end());
  if (!modal.actions.empty() && lines.size() < height) {
    std::string actions;
    for (std::size_t index = 0; index < modal.actions.size(); ++index) {
      if (!actions.empty()) {
        actions += ' ';
      }
      actions += '[';
      actions += modal.actions[index];
      actions += ']';
    }
    lines.push_back(std::move(actions));
  }
  if (lines.size() > height) {
    lines.resize(height);
  }
  return lines;
}

[[nodiscard]] std::vector<std::string> build_footer_lines(
    const model::TuiScreenModel& screen_model) {
  return {"focus=" + format_focus(screen_model.focus) +
          " | modal=" + format_modal_kind(screen_model.modal.kind) +
          " | debug=" +
          (trim_copy(screen_model.debug_reason).empty() ? std::string{"none"}
                                                        : trim_copy(screen_model.debug_reason))};
}

[[nodiscard]] std::vector<std::string> select_status_lines(
    const view::TuiStatusPanelRenderResult& rendered_status,
    const std::size_t max_lines,
    const view::TuiLayoutMode mode) {
  std::vector<std::string> selected;
  if (max_lines == 0) {
    return selected;
  }

  if (mode == view::TuiLayoutMode::Narrow &&
      rendered_status.lines.size() > max_lines) {
    constexpr std::array<std::size_t, 8> kPriorityOrder = {3U, 0U, 7U, 5U,
                                                           1U, 2U, 4U, 6U};
    for (const std::size_t line_index : kPriorityOrder) {
      if (line_index >= rendered_status.lines.size()) {
        continue;
      }
      selected.push_back(rendered_status.lines[line_index].text);
      if (selected.size() == max_lines) {
        return selected;
      }
    }
  }

  for (const auto& line : rendered_status.lines) {
    selected.push_back(line.text);
    if (selected.size() == max_lines) {
      break;
    }
  }
  return selected;
}

using Canvas = std::vector<std::vector<std::string>>;

void place_text(Canvas& canvas,
                const std::size_t row,
                const std::size_t column,
                const std::size_t width,
                std::string_view text,
                const bool clear_region = true) {
  if (row >= canvas.size() || column >= canvas[row].size() || width == 0) {
    return;
  }

  const std::size_t end_column = std::min(canvas[row].size(), column + width);
  if (clear_region) {
    for (std::size_t cell = column; cell < end_column; ++cell) {
      canvas[row][cell] = " ";
    }
  }

  std::size_t cursor = 0;
  std::size_t cell = column;
  while (cursor < text.size() && cell < end_column) {
    const view::TuiTerminalTextToken token =
        view::next_terminal_text_token(text, cursor);
    const std::size_t token_columns = token.columns;
    if (token_columns == 0) {
      if (cell > column) {
        canvas[row][cell - 1U] += token.valid ? std::string(token.bytes) : std::string{"?"};
      }
      cursor += token.bytes.empty() ? 1U : token.bytes.size();
      continue;
    }
    if (cell + token_columns > end_column) {
      break;
    }

    canvas[row][cell] = token.valid ? std::string(token.bytes) : std::string{"?"};
    for (std::size_t offset = 1U; offset < token_columns; ++offset) {
      canvas[row][cell + offset].clear();
    }
    cell += token_columns;
    cursor += token.bytes.empty() ? 1U : token.bytes.size();
  }
}

void draw_box(Canvas& canvas,
              const std::size_t x,
              const std::size_t y,
              const std::size_t width,
              const std::size_t height,
              std::string_view title,
              const std::vector<std::string>& lines) {
  if (width < 2 || height < 2 || y >= canvas.size() || x >= canvas.front().size()) {
    return;
  }

  const std::size_t box_width = std::min(width, canvas.front().size() - x);
  const std::size_t box_height = std::min(height, canvas.size() - y);
  if (box_width < 2 || box_height < 2) {
    return;
  }

  for (std::size_t row = 0; row < box_height; ++row) {
    for (std::size_t column = 0; column < box_width; ++column) {
      canvas[y + row][x + column] = " ";
    }
  }

  for (std::size_t column = 0; column < box_width; ++column) {
    canvas[y][x + column] = "-";
    canvas[y + box_height - 1][x + column] = "-";
  }
  for (std::size_t row = 0; row < box_height; ++row) {
    canvas[y + row][x] = "|";
    canvas[y + row][x + box_width - 1] = "|";
  }
  canvas[y][x] = "+";
  canvas[y][x + box_width - 1] = "+";
  canvas[y + box_height - 1][x] = "+";
  canvas[y + box_height - 1][x + box_width - 1] = "+";

  if (!title.empty() && box_width > 4) {
    const std::string rendered_title = "[" + uppercase_ascii(title) + "]";
    place_text(canvas, y, x + 2, box_width - 4, rendered_title, false);
  }

  const std::size_t content_height = box_height - 2;
  const std::size_t content_width = box_width - 2;
  const std::size_t visible_line_count = std::min(lines.size(), content_height);
  for (std::size_t index = 0; index < visible_line_count; ++index) {
    place_text(canvas, y + 1 + index, x + 1, content_width, lines[index]);
  }
}

[[nodiscard]] std::string render_ascii_screen(const TuiRenderFrame& frame) {
  const std::size_t width = clamp_nonzero(frame.metrics.terminal_width);
  const std::size_t height = clamp_nonzero(frame.metrics.terminal_height);
  Canvas canvas(height, std::vector<std::string>(width, " "));

  const std::size_t body_x = frame.metrics.outer_padding;
  const std::size_t body_y = frame.metrics.outer_padding + frame.metrics.header_height +
                             frame.metrics.section_gap;
  const std::size_t body_width = saturating_sub(width, frame.metrics.outer_padding * 2U);

  for (std::size_t index = 0; index < frame.header_lines.size(); ++index) {
    if (index >= frame.metrics.header_height) {
      break;
    }
    place_text(canvas,
               frame.metrics.outer_padding + index,
               frame.metrics.outer_padding,
               body_width,
               frame.header_lines[index]);
  }

  if (frame.metrics.mode == view::TuiLayoutMode::FullScreen) {
    draw_box(canvas,
             body_x,
             body_y,
             frame.metrics.transcript.width,
             frame.metrics.transcript.height,
             "transcript",
             frame.transcript_lines);
    draw_box(canvas,
             body_x + frame.metrics.transcript.width + frame.metrics.section_gap,
             body_y,
             frame.metrics.status_panel.width,
             frame.metrics.status_panel.height,
             "status",
             frame.status_lines);
  } else {
    draw_box(canvas,
             body_x,
             body_y,
             frame.metrics.transcript.width,
             frame.metrics.transcript.height,
             "transcript",
             frame.transcript_lines);

    if (frame.metrics.show_status_panel && frame.metrics.status_panel.visible) {
      const std::size_t status_y = body_y + frame.metrics.transcript.height +
                                   frame.metrics.section_gap;
      draw_box(canvas,
               body_x,
               status_y,
               frame.metrics.status_panel.width,
               frame.metrics.status_panel.height,
               "status",
               frame.status_lines);
    }
  }

  std::size_t body_region_height = frame.metrics.transcript.height;
  if (frame.metrics.mode == view::TuiLayoutMode::FullScreen) {
    body_region_height = std::max(frame.metrics.transcript.height,
                                  frame.metrics.status_panel.height);
  } else if (frame.metrics.show_status_panel && frame.metrics.status_panel.visible) {
    body_region_height = frame.metrics.transcript.height + frame.metrics.section_gap +
                         frame.metrics.status_panel.height;
  }

  const std::size_t composer_y = body_y + body_region_height + frame.metrics.section_gap;
  draw_box(canvas,
           body_x,
           composer_y,
           body_width,
           frame.metrics.composer_height,
           "composer " +
               (frame.composer_lines.empty() ? std::string{"ready"}
                                             : std::string{}),
           frame.composer_lines);

  if (!frame.footer_lines.empty()) {
    const std::size_t footer_y = height - frame.metrics.outer_padding -
                                 frame.metrics.footer_height;
    place_text(canvas,
               footer_y,
               frame.metrics.outer_padding,
               body_width,
               frame.footer_lines.front());
  }

  if (frame.metrics.modal.visible && !frame.modal_lines.empty()) {
    const std::size_t modal_x = (width - frame.metrics.modal.width) / 2U;
    const std::size_t modal_y = (height - frame.metrics.modal.height) / 2U;
    draw_box(canvas,
             modal_x,
             modal_y,
             frame.metrics.modal.width,
             frame.metrics.modal.height,
             "next turn preference",
             frame.modal_lines);
  }

  std::string rendered;
  rendered.reserve(height * (width + 1U));
  for (std::size_t index = 0; index < canvas.size(); ++index) {
    if (index != 0) {
      rendered.push_back('\n');
    }
    for (const std::string& cell : canvas[index]) {
      rendered += cell;
    }
  }
  return rendered;
}

#if defined(DASALL_TUI_RENDERER_USE_FTXUI) && DASALL_TUI_RENDERER_USE_FTXUI
[[nodiscard]] ftxui::Element make_panel(std::string_view title,
                                        const std::vector<std::string>& lines) {
  ftxui::Elements children;
  if (lines.empty()) {
    children.push_back(ftxui::text(""));
  } else {
    for (const std::string& line : lines) {
      children.push_back(ftxui::text(line));
    }
  }
  return ftxui::window(ftxui::text(uppercase_ascii(title)),
                       ftxui::vbox(std::move(children)));
}

[[nodiscard]] std::string render_ftxui_screen(const TuiRenderFrame& frame) {
  using namespace ftxui;

  Elements header_children;
  for (const std::string& line : frame.header_lines) {
    header_children.push_back(text(line));
  }

  Element body = make_panel("transcript", frame.transcript_lines);
  if (frame.metrics.mode == view::TuiLayoutMode::FullScreen) {
    body = hbox({make_panel("transcript", frame.transcript_lines) |
                     size(WIDTH, EQUAL, static_cast<int>(frame.metrics.transcript.width)),
                 make_panel("status", frame.status_lines) |
                     size(WIDTH, EQUAL, static_cast<int>(frame.metrics.status_panel.width))});
  } else if (frame.metrics.show_status_panel && frame.metrics.status_panel.visible) {
    body = vbox({make_panel("transcript", frame.transcript_lines),
                 make_panel("status", frame.status_lines)});
  }

  Element root = vbox({vbox(std::move(header_children)),
                       separator(),
                       body,
                       separator(),
                       make_panel("composer", frame.composer_lines),
                       text(frame.footer_lines.empty() ? std::string{} : frame.footer_lines.front())});

  if (frame.metrics.modal.visible && !frame.modal_lines.empty()) {
    root = dbox({root, make_panel("next turn preference", frame.modal_lines) | center});
  }

  Screen screen = Screen::Create(Dimension::Fixed(static_cast<int>(frame.metrics.terminal_width)),
                                 Dimension::Fixed(static_cast<int>(frame.metrics.terminal_height)));
  Render(screen, root);
  return screen.ToString();
}
#endif

}  // namespace

FtxuiRendererAdapter::FtxuiRendererAdapter(view::TuiDesignTokens tokens)
    : tokens_(std::move(tokens)) {}

const view::TuiDesignTokens& FtxuiRendererAdapter::design_tokens() const noexcept {
  return tokens_;
}

view::TuiLayoutMetrics FtxuiRendererAdapter::apply_layout_metrics(
    const std::size_t terminal_width,
    const std::size_t terminal_height) const noexcept {
  return view::TuiLayoutMetrics::for_terminal(terminal_width, terminal_height, tokens_);
}

TuiRenderFrame FtxuiRendererAdapter::render_root(
    const model::TuiScreenModel& screen_model,
    const std::size_t terminal_width,
    const std::size_t terminal_height) const {
  TuiRenderFrame frame;
  frame.metrics = apply_layout_metrics(terminal_width, terminal_height);
  frame.header_lines = build_header_lines(screen_model, frame.metrics);

  const std::size_t transcript_width =
      saturating_sub(frame.metrics.transcript.width, 2U);
  const std::size_t transcript_height =
      saturating_sub(frame.metrics.transcript.height, 2U);
  view::TuiTranscriptView transcript_view(screen_model.transcript);
  const auto transcript = transcript_view.render_transcript(
      transcript_height,
      transcript_width > tokens_.spacing.transcript_indent
          ? transcript_width - tokens_.spacing.transcript_indent
          : 1U);
  if (transcript.visible_lines.empty()) {
    frame.transcript_lines.push_back("[empty transcript]");
  } else {
    for (const auto& line : transcript.visible_lines) {
      frame.transcript_lines.push_back(line.text);
    }
  }

  if (frame.metrics.show_status_panel && frame.metrics.status_panel.visible) {
    view::TuiStatusPanel status_panel(screen_model.status);
    const auto rendered_status = status_panel.render_status_panel(
        saturating_sub(frame.metrics.status_panel.width, 2U));
    frame.status_lines = select_status_lines(
        rendered_status,
        saturating_sub(frame.metrics.status_panel.height, 2U),
        frame.metrics.mode);
  }

  frame.composer_lines = build_composer_lines(
      screen_model.composer,
      saturating_sub(frame.metrics.terminal_width - frame.metrics.outer_padding * 2U, 2U),
      saturating_sub(frame.metrics.composer_height, 2U));
  frame.footer_lines = build_footer_lines(screen_model);

  if (screen_model.modal.kind != model::TuiModalKind::None && frame.metrics.modal.visible) {
    frame.modal_lines = build_modal_lines(
        screen_model.modal,
        saturating_sub(frame.metrics.modal.width, 2U),
        saturating_sub(frame.metrics.modal.height, 2U));
  }

  return frame;
}

std::string FtxuiRendererAdapter::render_to_screen(
    const model::TuiScreenModel& screen_model,
    const std::size_t terminal_width,
    const std::size_t terminal_height) const {
  const TuiRenderFrame frame = render_root(screen_model, terminal_width, terminal_height);

#if defined(DASALL_TUI_RENDERER_USE_FTXUI) && DASALL_TUI_RENDERER_USE_FTXUI
  return render_ftxui_screen(frame);
#else
  return render_ascii_screen(frame);
#endif
}

}  // namespace dasall::tui::terminal