#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::tui::view {

struct TuiTerminalTextToken {
  std::string_view bytes;
  std::size_t columns{0};
  bool valid{false};
};

namespace detail {

[[nodiscard]] inline bool is_utf8_continuation(const unsigned char byte) noexcept {
  return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] inline bool is_zero_width_code_point(const std::uint32_t code_point) noexcept {
  return (code_point >= 0x0300U && code_point <= 0x036FU) ||
      (code_point >= 0x1AB0U && code_point <= 0x1AFFU) ||
      (code_point >= 0x1DC0U && code_point <= 0x1DFFU) ||
      code_point == 0x200DU ||
      (code_point >= 0x20D0U && code_point <= 0x20FFU) ||
      (code_point >= 0xFE00U && code_point <= 0xFE0FU) ||
      (code_point >= 0xFE20U && code_point <= 0xFE2FU);
}

[[nodiscard]] inline bool is_wide_code_point(const std::uint32_t code_point) noexcept {
  return (code_point >= 0x1100U && code_point <= 0x115FU) ||
      code_point == 0x2329U || code_point == 0x232AU ||
      (code_point >= 0x2E80U && code_point <= 0xA4CFU && code_point != 0x303FU) ||
      (code_point >= 0xAC00U && code_point <= 0xD7A3U) ||
      (code_point >= 0xF900U && code_point <= 0xFAFFU) ||
      (code_point >= 0xFE10U && code_point <= 0xFE19U) ||
      (code_point >= 0xFE30U && code_point <= 0xFE6FU) ||
      (code_point >= 0xFF00U && code_point <= 0xFF60U) ||
      (code_point >= 0xFFE0U && code_point <= 0xFFE6U) ||
      (code_point >= 0x1F300U && code_point <= 0x1FAFFU) ||
      (code_point >= 0x20000U && code_point <= 0x3FFFD);
}

[[nodiscard]] inline std::size_t code_point_columns(const std::uint32_t code_point) noexcept {
  if (code_point == '\t') {
    return 1U;
  }
  if (code_point < 0x20U || (code_point >= 0x7FU && code_point < 0xA0U)) {
    return 0U;
  }
  if (is_zero_width_code_point(code_point)) {
    return 0U;
  }
  return is_wide_code_point(code_point) ? 2U : 1U;
}

[[nodiscard]] inline bool is_valid_code_point(const std::uint32_t code_point) noexcept {
  return code_point <= 0x10FFFFU && !(code_point >= 0xD800U && code_point <= 0xDFFFU);
}

inline void trim_trailing_ascii_spaces(std::string& text) {
  while (!text.empty() && text.back() == ' ') {
    text.pop_back();
  }
}

}  // namespace detail

[[nodiscard]] inline TuiTerminalTextToken next_terminal_text_token(
    std::string_view text,
    const std::size_t offset) noexcept {
  if (offset >= text.size()) {
    return {};
  }

  const unsigned char lead = static_cast<unsigned char>(text[offset]);
  if (lead < 0x80U) {
    return TuiTerminalTextToken{.bytes = text.substr(offset, 1U),
                                .columns = detail::code_point_columns(lead),
                                .valid = true};
  }

  std::size_t length = 0;
  std::uint32_t code_point = 0;
  if ((lead & 0xE0U) == 0xC0U) {
    length = 2U;
    code_point = lead & 0x1FU;
  } else if ((lead & 0xF0U) == 0xE0U) {
    length = 3U;
    code_point = lead & 0x0FU;
  } else if ((lead & 0xF8U) == 0xF0U) {
    length = 4U;
    code_point = lead & 0x07U;
  } else {
    return TuiTerminalTextToken{.bytes = text.substr(offset, 1U),
                                .columns = 1U,
                                .valid = false};
  }

  if (offset + length > text.size()) {
    return TuiTerminalTextToken{.bytes = text.substr(offset, 1U),
                                .columns = 1U,
                                .valid = false};
  }

  for (std::size_t index = 1U; index < length; ++index) {
    const unsigned char byte = static_cast<unsigned char>(text[offset + index]);
    if (!detail::is_utf8_continuation(byte)) {
      return TuiTerminalTextToken{.bytes = text.substr(offset, 1U),
                                  .columns = 1U,
                                  .valid = false};
    }
    code_point = (code_point << 6U) | (byte & 0x3FU);
  }

  const bool overlong = (length == 2U && code_point < 0x80U) ||
      (length == 3U && code_point < 0x800U) ||
      (length == 4U && code_point < 0x10000U);
  if (overlong || !detail::is_valid_code_point(code_point)) {
    return TuiTerminalTextToken{.bytes = text.substr(offset, 1U),
                                .columns = 1U,
                                .valid = false};
  }

  return TuiTerminalTextToken{.bytes = text.substr(offset, length),
                              .columns = detail::code_point_columns(code_point),
                              .valid = true};
}

[[nodiscard]] inline std::size_t clamp_to_terminal_text_offset(
    std::string_view text,
    const std::size_t offset) noexcept {
  if (offset >= text.size()) {
    return text.size();
  }

  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const TuiTerminalTextToken token = next_terminal_text_token(text, cursor);
    const std::size_t next = cursor + (token.bytes.empty() ? 1U : token.bytes.size());
    if (next > offset) {
      return cursor;
    }
    cursor = next;
  }
  return text.size();
}

[[nodiscard]] inline std::size_t next_terminal_text_offset(
    std::string_view text,
    const std::size_t offset) noexcept {
  const std::size_t cursor = clamp_to_terminal_text_offset(text, offset);
  if (cursor >= text.size()) {
    return text.size();
  }

  const TuiTerminalTextToken token = next_terminal_text_token(text, cursor);
  return cursor + (token.bytes.empty() ? 1U : token.bytes.size());
}

[[nodiscard]] inline std::size_t previous_terminal_text_offset(
    std::string_view text,
    const std::size_t offset) noexcept {
  const std::size_t target = clamp_to_terminal_text_offset(text, offset);
  if (target == 0U) {
    return 0U;
  }

  std::size_t previous = 0;
  std::size_t cursor = 0;
  while (cursor < target) {
    previous = cursor;
    const TuiTerminalTextToken token = next_terminal_text_token(text, cursor);
    cursor += token.bytes.empty() ? 1U : token.bytes.size();
  }
  return previous;
}

[[nodiscard]] inline std::size_t terminal_display_width(std::string_view text) noexcept {
  std::size_t width = 0;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const TuiTerminalTextToken token = next_terminal_text_token(text, cursor);
    width += token.columns;
    cursor += token.bytes.empty() ? 1U : token.bytes.size();
  }
  return width;
}

[[nodiscard]] inline std::string truncate_to_terminal_width(std::string_view text,
                                                            const std::size_t width) {
  std::string rendered;
  rendered.reserve(text.size());
  std::size_t used = 0;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const TuiTerminalTextToken token = next_terminal_text_token(text, cursor);
    const std::size_t token_columns = token.columns;
    if (token_columns > 0 && used + token_columns > width) {
      break;
    }
    rendered += token.valid ? std::string(token.bytes) : std::string{"?"};
    used += token_columns;
    cursor += token.bytes.empty() ? 1U : token.bytes.size();
  }
  return rendered;
}

[[nodiscard]] inline std::string pad_to_terminal_width(std::string_view text,
                                                       const std::size_t width) {
  std::string rendered = truncate_to_terminal_width(text, width);
  const std::size_t rendered_width = terminal_display_width(rendered);
  if (rendered_width < width) {
    rendered.append(width - rendered_width, ' ');
  }
  return rendered;
}

[[nodiscard]] inline std::vector<std::string> wrap_to_terminal_width(
    std::string_view text,
    const std::size_t width,
    const std::size_t max_lines = std::numeric_limits<std::size_t>::max()) {
  std::vector<std::string> lines;
  if (max_lines == 0) {
    return lines;
  }

  const std::size_t safe_width = width == 0 ? 1U : width;
  std::size_t paragraph_start = 0;
  while (paragraph_start <= text.size() && lines.size() < max_lines) {
    const std::size_t paragraph_end = text.find('\n', paragraph_start);
    const std::string_view paragraph = paragraph_end == std::string_view::npos
                                           ? text.substr(paragraph_start)
                                           : text.substr(paragraph_start,
                                                         paragraph_end - paragraph_start);
    std::string current;
    std::size_t current_width = 0;
    std::size_t last_space_byte = std::string::npos;
    std::size_t cursor = 0;
    while (cursor < paragraph.size() && lines.size() < max_lines) {
      const TuiTerminalTextToken token = next_terminal_text_token(paragraph, cursor);
      const bool ascii_space = token.valid && token.bytes.size() == 1U && token.bytes.front() == ' ';
      if (ascii_space && current.empty()) {
        cursor += token.bytes.size();
        continue;
      }

      if (token.columns > safe_width) {
        detail::trim_trailing_ascii_spaces(current);
        if (!current.empty()) {
          lines.push_back(std::move(current));
          current = {};
          current_width = 0;
          last_space_byte = std::string::npos;
        }
        if (lines.size() < max_lines) {
          lines.push_back("?");
        }
        cursor += token.bytes.empty() ? 1U : token.bytes.size();
        continue;
      }

      if (token.columns > 0 && current_width + token.columns > safe_width) {
        if (last_space_byte != std::string::npos) {
          std::string line = current.substr(0, last_space_byte);
          detail::trim_trailing_ascii_spaces(line);
          if (!line.empty()) {
            lines.push_back(std::move(line));
          }
          current.erase(0, last_space_byte + 1U);
          current_width = terminal_display_width(current);
          last_space_byte = current.find_last_of(' ');
          if (ascii_space && current.empty()) {
            cursor += token.bytes.size();
          }
          continue;
        }

        detail::trim_trailing_ascii_spaces(current);
        if (!current.empty()) {
          lines.push_back(std::move(current));
        }
        current = {};
        current_width = 0;
        last_space_byte = std::string::npos;
        if (ascii_space) {
          cursor += token.bytes.size();
        }
        continue;
      }

      if (ascii_space) {
        last_space_byte = current.size();
      }
      current += token.valid ? std::string(token.bytes) : std::string{"?"};
      current_width += token.columns;
      cursor += token.bytes.empty() ? 1U : token.bytes.size();
    }

    if (lines.size() < max_lines) {
      detail::trim_trailing_ascii_spaces(current);
      lines.push_back(std::move(current));
    }

    if (paragraph_end == std::string_view::npos) {
      break;
    }
    paragraph_start = paragraph_end + 1U;
  }

  if (lines.empty()) {
    lines.emplace_back();
  }
  return lines;
}

}  // namespace dasall::tui::view