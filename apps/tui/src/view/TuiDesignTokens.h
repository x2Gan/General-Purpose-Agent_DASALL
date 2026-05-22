#pragma once

#include <cstddef>
#include <cstdint>

namespace dasall::tui::view {

struct TuiColorToken {
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  bool bold = false;
  bool inverted = false;
};

struct TuiSpacingTokens {
  std::size_t outer_padding = 1;
  std::size_t section_gap = 1;
  std::size_t panel_padding = 1;
  std::size_t transcript_indent = 2;
  std::size_t composer_min_lines = 3;
  std::size_t composer_max_lines = 6;
};

struct TuiBadgeTokens {
  std::size_t horizontal_padding = 1;
  std::size_t vertical_padding = 0;
  bool uppercase = true;
};

struct TuiFocusTokens {
  std::size_t border_thickness = 1;
  bool bold_label = true;
};

struct TuiBreakpointTokens {
  std::size_t minimum_width = 40;
  std::size_t minimum_height = 12;
  std::size_t narrow_width = 80;
  std::size_t narrow_height = 24;
  std::size_t full_width = 120;
  std::size_t full_height = 36;
  std::size_t full_status_panel_width = 34;
  std::size_t narrow_status_panel_height = 8;
};

struct TuiDesignTokens {
  TuiColorToken canvas{0x11, 0x18, 0x22, false, false};
  TuiColorToken surface{0x1A, 0x24, 0x31, false, false};
  TuiColorToken text{0xF2, 0xEE, 0xE7, false, false};
  TuiColorToken accent{0x2F, 0xA0, 0x9E, true, false};
  TuiColorToken muted{0x96, 0xA1, 0xB3, false, false};
  TuiColorToken success{0x7A, 0xC7, 0x4F, true, false};
  TuiColorToken warning{0xE7, 0xB4, 0x16, true, false};
  TuiColorToken danger{0xE3, 0x64, 0x64, true, false};
  TuiColorToken focus{0x5B, 0xD1, 0xFF, true, false};

  TuiSpacingTokens spacing{};
  TuiBadgeTokens badges{};
  TuiFocusTokens focus_style{};
  TuiBreakpointTokens breakpoints{};

  [[nodiscard]] static constexpr TuiDesignTokens defaults() noexcept {
    return {};
  }
};

}  // namespace dasall::tui::view