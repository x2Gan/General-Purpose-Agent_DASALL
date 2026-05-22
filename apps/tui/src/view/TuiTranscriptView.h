#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "model/TuiScreenModel.h"

namespace dasall::tui::view {

struct TuiTranscriptLine {
  std::size_t message_index = 0;
  std::string text;
};

struct TuiTranscriptRenderResult {
  std::vector<TuiTranscriptLine> visible_lines;
  std::size_t total_line_count = 0;
  std::size_t scroll_offset = 0;
  bool at_top = true;
  bool at_bottom = true;
};

class TuiTranscriptView {
 public:
  explicit TuiTranscriptView(std::vector<model::TuiMessageView> transcript = {});

  void set_transcript(std::vector<model::TuiMessageView> transcript);

  [[nodiscard]] const std::vector<model::TuiMessageView>& transcript() const noexcept;

  [[nodiscard]] std::size_t scroll_offset() const noexcept;

  [[nodiscard]] TuiTranscriptRenderResult render_transcript(
      std::size_t viewport_height,
      std::size_t wrap_width = 80) const;

  [[nodiscard]] bool toggle_collapse(std::size_t message_index);

  void scroll_to_bottom(std::size_t viewport_height,
                        std::size_t wrap_width = 80);

 private:
  [[nodiscard]] std::vector<TuiTranscriptLine> flatten_transcript(
      std::size_t wrap_width) const;

  std::vector<model::TuiMessageView> transcript_;
  std::size_t scroll_offset_ = 0;
};

}  // namespace dasall::tui::view