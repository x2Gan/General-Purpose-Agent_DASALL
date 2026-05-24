#pragma once

#include <cstddef>

#include "view/TuiDesignTokens.h"

namespace dasall::tui::view {

enum class TuiLayoutMode {
  FullScreen,
  Narrow,
  Line,
};

struct TuiLayoutRegionMetrics {
  std::size_t width = 0;
  std::size_t height = 0;
  bool visible = false;
};

struct TuiLayoutMetrics {
  TuiLayoutMode mode = TuiLayoutMode::Line;
  std::size_t terminal_width = 0;
  std::size_t terminal_height = 0;
  std::size_t outer_padding = 1;
  std::size_t section_gap = 1;
  std::size_t header_height = 2;
  std::size_t composer_height = 3;
  std::size_t footer_height = 1;
  bool show_selector_strip = false;
  bool show_status_panel = false;
  bool status_panel_stacked = false;
  TuiLayoutRegionMetrics transcript;
  TuiLayoutRegionMetrics status_panel;
  TuiLayoutRegionMetrics modal;

  [[nodiscard]] static constexpr TuiLayoutMetrics for_terminal(
      std::size_t width,
      std::size_t height,
      const TuiDesignTokens& tokens = TuiDesignTokens::defaults()) noexcept;
};

namespace detail {

[[nodiscard]] constexpr std::size_t clamp_nonzero(std::size_t value) noexcept {
  return value == 0 ? 1U : value;
}

[[nodiscard]] constexpr std::size_t max_value(std::size_t left,
                                              std::size_t right) noexcept {
  return left > right ? left : right;
}

[[nodiscard]] constexpr std::size_t min_value(std::size_t left,
                                              std::size_t right) noexcept {
  return left < right ? left : right;
}

[[nodiscard]] constexpr std::size_t saturating_sub(std::size_t left,
                                                   std::size_t right) noexcept {
  return left > right ? left - right : 0U;
}

}  // namespace detail

constexpr TuiLayoutMetrics TuiLayoutMetrics::for_terminal(
    std::size_t width,
    std::size_t height,
    const TuiDesignTokens& tokens) noexcept {
  TuiLayoutMetrics metrics;
  metrics.terminal_width = detail::clamp_nonzero(width);
  metrics.terminal_height = detail::clamp_nonzero(height);
  metrics.outer_padding = tokens.spacing.outer_padding;
  metrics.section_gap = tokens.spacing.section_gap;
  metrics.header_height = 3;
  metrics.composer_height = detail::max_value(tokens.spacing.composer_min_lines, 4U);
  metrics.footer_height = 1;
  metrics.show_selector_strip = true;
  metrics.show_status_panel = true;

  const std::size_t full_width_floor = detail::saturating_sub(
      tokens.breakpoints.full_width,
      1U);

  if (metrics.terminal_width >= full_width_floor &&
      metrics.terminal_height >= tokens.breakpoints.full_height) {
    metrics.mode = TuiLayoutMode::FullScreen;
    metrics.composer_height =
        detail::min_value(tokens.spacing.composer_max_lines, 5U);
  } else if (metrics.terminal_width >= tokens.breakpoints.narrow_width &&
             metrics.terminal_height >= tokens.breakpoints.narrow_height) {
    metrics.mode = TuiLayoutMode::Narrow;
  } else {
    metrics.mode = TuiLayoutMode::Line;
    metrics.header_height = 2;
    metrics.composer_height = tokens.spacing.composer_min_lines;
    metrics.show_selector_strip = false;
    metrics.show_status_panel = false;
  }

  const std::size_t body_width = detail::saturating_sub(
      metrics.terminal_width,
      metrics.outer_padding * 2U);
  const std::size_t reserved_vertical =
      metrics.outer_padding * 2U + metrics.header_height + metrics.composer_height +
      metrics.footer_height + metrics.section_gap * 2U;
  const std::size_t body_height = detail::saturating_sub(
      metrics.terminal_height,
      reserved_vertical);

  metrics.transcript.visible = body_height > 0U;
  metrics.transcript.width = body_width;
  metrics.transcript.height = body_height;

  if (metrics.mode == TuiLayoutMode::FullScreen) {
    metrics.status_panel.visible = true;
    metrics.status_panel_stacked = false;
    metrics.status_panel.width = detail::min_value(
        body_width,
        detail::max_value(tokens.breakpoints.full_status_panel_width, body_width / 3U));
    metrics.status_panel.height = body_height;
    metrics.transcript.width = detail::saturating_sub(
        body_width,
        metrics.status_panel.width + metrics.section_gap);
  } else if (metrics.mode == TuiLayoutMode::Narrow) {
    constexpr std::size_t kMinimumTranscriptHeight = 8U;

    metrics.status_panel.visible = true;
    metrics.status_panel_stacked = true;
    metrics.status_panel.width = body_width;
    metrics.status_panel.height = detail::min_value(
        tokens.breakpoints.narrow_status_panel_height,
        detail::max_value(6U, body_height / 3U));
    metrics.transcript.height = detail::saturating_sub(
        body_height,
        metrics.status_panel.height + metrics.section_gap);

    if (body_height > kMinimumTranscriptHeight + metrics.section_gap &&
        metrics.transcript.height < kMinimumTranscriptHeight) {
      metrics.status_panel.height = detail::saturating_sub(
          body_height,
          kMinimumTranscriptHeight + metrics.section_gap);
      metrics.transcript.height = kMinimumTranscriptHeight;
    }
  }

  metrics.modal.visible = metrics.mode != TuiLayoutMode::Line;
  metrics.modal.width = detail::min_value(
      body_width,
      detail::max_value(36U, detail::saturating_sub(body_width, 4U)));
  metrics.modal.height = detail::min_value(
      body_height,
      detail::max_value(8U, detail::saturating_sub(body_height, 4U)));
  return metrics;
}

}  // namespace dasall::tui::view