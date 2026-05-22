#include "view/TuiTranscriptView.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>

namespace dasall::tui::view {
namespace {

constexpr std::size_t kBodyIndent = 2;
constexpr std::string_view kUnsafeSummary = "[redacted unsafe transcript summary]";
constexpr std::array<std::string_view, 12> kForbiddenMarkers = {
    "chain-of-thought",
    "chain of thought",
    "reasoning_content",
    "reasoning content",
    "private reasoning",
    "provider-private reasoning",
    "raw tool output",
    "stdout:",
    "stderr:",
    "api_key",
    "authorization:",
    "bearer ",
};

[[nodiscard]] std::size_t clamp_width(std::size_t width) noexcept {
  return width == 0 ? 1U : width;
}

[[nodiscard]] std::string lowercase_copy(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const unsigned char character : text) {
    lowered.push_back(static_cast<char>(std::tolower(character)));
  }
  return lowered;
}

[[nodiscard]] bool contains_forbidden_marker(std::string_view text) {
  if (text.empty()) {
    return false;
  }

  const std::string lowered = lowercase_copy(text);
  for (const std::string_view marker : kForbiddenMarkers) {
    if (lowered.find(marker) != std::string::npos) {
      return true;
    }
  }

  return lowered.find("sk-") != std::string::npos;
}

[[nodiscard]] std::string normalize_role(std::string_view role) {
  std::string normalized = role.empty() ? std::string{"system"} : std::string(role);
  normalized.front() = static_cast<char>(std::toupper(
      static_cast<unsigned char>(normalized.front())));
  return normalized;
}

[[nodiscard]] std::string sanitize_text(std::string_view text) {
  if (text.empty()) {
    return {};
  }

  if (contains_forbidden_marker(text)) {
    return std::string(kUnsafeSummary);
  }

  return std::string(text);
}

void trim_trailing_spaces(std::string& text) {
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
}

[[nodiscard]] std::vector<std::string> wrap_paragraph(std::string_view paragraph,
                                                      std::size_t width) {
  std::vector<std::string> lines;
  const std::size_t safe_width = clamp_width(width);
  std::size_t cursor = 0;

  while (cursor < paragraph.size()) {
    while (cursor < paragraph.size() && paragraph[cursor] == ' ') {
      ++cursor;
    }
    if (cursor >= paragraph.size()) {
      break;
    }

    const std::size_t limit = std::min(cursor + safe_width, paragraph.size());
    std::size_t break_at = limit;
    if (limit < paragraph.size()) {
      const std::size_t last_space = paragraph.rfind(' ', limit - 1);
      if (last_space != std::string_view::npos && last_space >= cursor) {
        break_at = last_space;
      }
    }

    if (break_at == cursor) {
      break_at = limit;
    }

    std::string line(paragraph.substr(cursor, break_at - cursor));
    trim_trailing_spaces(line);
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }

    cursor = break_at;
    while (cursor < paragraph.size() && paragraph[cursor] == ' ') {
      ++cursor;
    }
  }

  if (lines.empty()) {
    lines.emplace_back();
  }

  return lines;
}

[[nodiscard]] std::vector<std::string> wrap_text(std::string_view text,
                                                 std::size_t width) {
  std::vector<std::string> lines;
  std::size_t paragraph_start = 0;

  while (paragraph_start <= text.size()) {
    const std::size_t paragraph_end = text.find('\n', paragraph_start);
    const std::string_view paragraph = paragraph_end == std::string_view::npos
                                           ? text.substr(paragraph_start)
                                           : text.substr(paragraph_start,
                                                         paragraph_end - paragraph_start);
    auto wrapped = wrap_paragraph(paragraph, width);
    lines.insert(lines.end(),
                 std::make_move_iterator(wrapped.begin()),
                 std::make_move_iterator(wrapped.end()));

    if (paragraph_end == std::string_view::npos) {
      break;
    }
    paragraph_start = paragraph_end + 1;
  }

  return lines;
}

[[nodiscard]] std::string collapse_preview(std::string_view text,
                                           std::size_t width) {
  std::string preview;
  preview.reserve(text.size());
  bool previous_space = false;

  for (const char character : text) {
    const bool is_space = character == ' ' || character == '\n' || character == '\r' ||
                          character == '\t';
    if (is_space) {
      if (!preview.empty() && !previous_space) {
        preview.push_back(' ');
      }
      previous_space = true;
      continue;
    }

    preview.push_back(character);
    previous_space = false;
  }

  trim_trailing_spaces(preview);
  const std::size_t safe_width = clamp_width(width);
  if (preview.size() <= safe_width) {
    return preview;
  }

  if (safe_width <= 3) {
    return std::string(safe_width, '.');
  }

  return preview.substr(0, safe_width - 3) + "...";
}

[[nodiscard]] std::string join_badges(const std::vector<std::string>& badges) {
  std::string joined;
  for (const std::string& badge : badges) {
    if (badge.empty() || contains_forbidden_marker(badge)) {
      continue;
    }

    if (!joined.empty()) {
      joined += ", ";
    }
    joined += badge;
  }
  return joined;
}

[[nodiscard]] std::size_t max_scroll_offset(std::size_t total_line_count,
                                            std::size_t viewport_height) noexcept {
  if (viewport_height == 0 || total_line_count <= viewport_height) {
    return 0;
  }

  return total_line_count - viewport_height;
}

}  // namespace

TuiTranscriptView::TuiTranscriptView(std::vector<model::TuiMessageView> transcript)
    : transcript_(std::move(transcript)) {}

void TuiTranscriptView::set_transcript(std::vector<model::TuiMessageView> transcript) {
  transcript_ = std::move(transcript);
  scroll_offset_ = 0;
}

const std::vector<model::TuiMessageView>& TuiTranscriptView::transcript() const noexcept {
  return transcript_;
}

std::size_t TuiTranscriptView::scroll_offset() const noexcept {
  return scroll_offset_;
}

TuiTranscriptRenderResult TuiTranscriptView::render_transcript(
    std::size_t viewport_height,
    std::size_t wrap_width) const {
  TuiTranscriptRenderResult result;
  const auto flattened = flatten_transcript(wrap_width);
  result.total_line_count = flattened.size();

  const std::size_t clamped_offset =
      std::min(scroll_offset_, max_scroll_offset(flattened.size(), viewport_height));
  result.scroll_offset = clamped_offset;
  result.at_top = clamped_offset == 0;
  result.at_bottom =
      clamped_offset >= max_scroll_offset(flattened.size(), viewport_height);

  if (viewport_height == 0 || flattened.empty()) {
    return result;
  }

  const std::size_t visible_end =
      std::min(flattened.size(), clamped_offset + viewport_height);
  result.visible_lines.insert(result.visible_lines.end(),
                              flattened.begin() + static_cast<std::ptrdiff_t>(clamped_offset),
                              flattened.begin() + static_cast<std::ptrdiff_t>(visible_end));
  return result;
}

bool TuiTranscriptView::toggle_collapse(std::size_t message_index) {
  if (message_index >= transcript_.size()) {
    return false;
  }

  model::TuiMessageView& message = transcript_[message_index];
  if (!message.collapsible) {
    return false;
  }

  message.collapsed = !message.collapsed;
  return true;
}

void TuiTranscriptView::scroll_to_bottom(std::size_t viewport_height,
                                         std::size_t wrap_width) {
  const auto flattened = flatten_transcript(wrap_width);
  scroll_offset_ = max_scroll_offset(flattened.size(), viewport_height);
}

std::vector<TuiTranscriptLine> TuiTranscriptView::flatten_transcript(
    std::size_t wrap_width) const {
  std::vector<TuiTranscriptLine> lines;
  const std::size_t safe_width = clamp_width(wrap_width);
  const std::size_t body_width = safe_width > kBodyIndent ? safe_width - kBodyIndent : 1U;

  for (std::size_t index = 0; index < transcript_.size(); ++index) {
    const model::TuiMessageView& message = transcript_[index];

    std::string header = normalize_role(message.role);
    if (!message.timestamp.empty()) {
      header += " | ";
      header += message.timestamp;
    }

    std::vector<std::string> header_badges = message.badges;
    if (message.collapsible && message.collapsed) {
      header_badges.push_back("collapsed");
    }
    const std::string joined_badges = join_badges(header_badges);
    if (!joined_badges.empty()) {
      header += " | ";
      header += joined_badges;
    }

    lines.push_back(TuiTranscriptLine{.message_index = index, .text = std::move(header)});

    const std::string safe_content = sanitize_text(message.content);
    if (safe_content.empty()) {
      continue;
    }

    std::vector<std::string> body_lines;
    if (message.collapsible && message.collapsed) {
      body_lines.push_back(collapse_preview(safe_content, body_width));
      if (!body_lines.back().empty()) {
        body_lines.back() += " [collapsed]";
      }
    } else {
      body_lines = wrap_text(safe_content, body_width);
    }

    for (std::string& body_line : body_lines) {
      lines.push_back(
          TuiTranscriptLine{.message_index = index, .text = std::string(kBodyIndent, ' ') + body_line});
    }
  }

  return lines;
}

}  // namespace dasall::tui::view