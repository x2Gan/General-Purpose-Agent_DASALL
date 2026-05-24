#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "model/TuiScreenModel.h"
#include "view/TuiDesignTokens.h"
#include "view/TuiLayoutMetrics.h"

namespace dasall::tui::terminal {

struct TuiRenderFrame {
  view::TuiLayoutMetrics metrics;
  std::vector<std::string> header_lines;
  std::vector<std::string> transcript_lines;
  std::vector<std::string> status_lines;
  std::vector<std::string> composer_lines;
  std::vector<std::string> footer_lines;
    std::string modal_title;
  std::vector<std::string> modal_lines;
};

class FtxuiRendererAdapter {
 public:
  explicit FtxuiRendererAdapter(
      view::TuiDesignTokens tokens = view::TuiDesignTokens::defaults());

  [[nodiscard]] const view::TuiDesignTokens& design_tokens() const noexcept;

  [[nodiscard]] view::TuiLayoutMetrics apply_layout_metrics(
      std::size_t terminal_width,
      std::size_t terminal_height) const noexcept;

  [[nodiscard]] TuiRenderFrame render_root(
      const model::TuiScreenModel& screen_model,
      std::size_t terminal_width,
      std::size_t terminal_height) const;

  [[nodiscard]] std::string render_to_screen(
      const model::TuiScreenModel& screen_model,
      std::size_t terminal_width,
      std::size_t terminal_height) const;

 private:
  view::TuiDesignTokens tokens_;
};

}  // namespace dasall::tui::terminal