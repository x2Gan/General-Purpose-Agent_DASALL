#include "view/TuiStatusPanel.h"

#include <cctype>
#include <string_view>
#include <utility>

namespace dasall::tui::view {
namespace {

constexpr std::size_t kNarrowPanelWidth = 32;

[[nodiscard]] std::string lowercase_copy(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const unsigned char character : text) {
    lowered.push_back(static_cast<char>(std::tolower(character)));
  }
  return lowered;
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

[[nodiscard]] std::string normalize_token(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());

  bool previous_space = false;
  for (const char character : text) {
    if (character == '_' || std::isspace(static_cast<unsigned char>(character)) != 0) {
      if (!normalized.empty() && !previous_space) {
        normalized.push_back(' ');
      }
      previous_space = true;
      continue;
    }

    normalized.push_back(character);
    previous_space = false;
  }

  if (!normalized.empty() && normalized.back() == ' ') {
    normalized.pop_back();
  }

  return normalized;
}

[[nodiscard]] bool contains_degraded_marker(std::string_view text) {
  const std::string lowered = lowercase_copy(text);
  return lowered.find("unknown") != std::string::npos ||
         lowered.find("degraded") != std::string::npos;
}

[[nodiscard]] bool is_normal_safe_mode(std::string_view text) {
  const std::string lowered = lowercase_copy(normalize_token(text));
  return lowered.empty() || lowered == "normal";
}

[[nodiscard]] bool is_healthy_status(std::string_view text) {
  const std::string lowered = lowercase_copy(normalize_token(text));
  return lowered == "healthy";
}

[[nodiscard]] std::string current_tool_summary(const data::TuiStatusProjection& status) {
  const std::string tool = trim_copy(status.current_tool);
  if (!tool.empty()) {
    return tool;
  }

  return "none";
}

[[nodiscard]] std::string pending_summary(const data::TuiStatusProjection& status) {
  const std::string pending = normalize_token(status.pending_interaction);
  if (!pending.empty()) {
    return pending;
  }

  return "none";
}

[[nodiscard]] std::string budget_summary(const data::TuiStatusProjection& status) {
  const std::string budget = trim_copy(status.budget_summary);
  if (!budget.empty()) {
    return budget;
  }

  return "degraded: unknown budget";
}

[[nodiscard]] std::string recovery_summary(const data::TuiStatusProjection& status) {
  const std::string recovery = trim_copy(status.recovery_summary);
  if (!recovery.empty()) {
    return recovery;
  }

  return "none";
}

[[nodiscard]] std::string safe_mode_summary(const data::TuiStatusProjection& status) {
  const std::string safe_mode = normalize_token(status.safe_mode_summary);
  if (!safe_mode.empty()) {
    return safe_mode;
  }

  return "degraded: unknown safe mode";
}

[[nodiscard]] std::string short_label(std::string_view label) {
  if (label == "stage") {
    return "stg";
  }
  if (label == "tool") {
    return "tool";
  }
  if (label == "pending") {
    return "pend";
  }
  if (label == "budget") {
    return "bdgt";
  }
  if (label == "recovery") {
    return "recv";
  }
  if (label == "health") {
    return "hlth";
  }
  if (label == "safe mode") {
    return "safe";
  }
  if (label == "decision") {
    return "dec";
  }
  return std::string(label);
}

[[nodiscard]] std::string make_line(std::string_view label,
                                    std::string_view value,
                                    bool narrow_layout) {
  const std::string rendered_label =
      narrow_layout ? short_label(label) : std::string(label);
  return rendered_label + ": " + std::string(value);
}

}  // namespace

TuiStatusPanel::TuiStatusPanel(data::TuiStatusProjection status)
    : status_(std::move(status)) {}

void TuiStatusPanel::set_status(data::TuiStatusProjection status) {
  status_ = std::move(status);
}

const data::TuiStatusProjection& TuiStatusPanel::status() const noexcept {
  return status_;
}

TuiStatusPanelRenderResult TuiStatusPanel::render_status_panel(
    const std::size_t panel_width) const {
  TuiStatusPanelRenderResult result;
  result.narrow_layout = panel_width > 0 && panel_width < kNarrowPanelWidth;
  result.stage_badge = format_stage_badge();
  result.health_summary = format_health_summary();
  result.decision_summary = format_decision_summary();

  const auto append_line = [&](std::string_view label, const std::string& value) {
    const bool degraded = contains_degraded_marker(value);
    result.degraded = result.degraded || degraded;
    result.lines.push_back(
        TuiStatusPanelLine{.text = make_line(label, value, result.narrow_layout),
                           .degraded = degraded});
  };

  append_line("stage", result.stage_badge);
  append_line("tool", current_tool_summary(status_));
  append_line("pending", pending_summary(status_));
  append_line("budget", budget_summary(status_));
  append_line("recovery", recovery_summary(status_));
  append_line("health", result.health_summary);
  append_line("safe mode", safe_mode_summary(status_));
  append_line("decision", result.decision_summary);
  return result;
}

std::string TuiStatusPanel::format_stage_badge() const {
  const std::string stage = normalize_token(status_.stage);
  if (stage.empty()) {
    return "[unknown stage]";
  }

  const std::string safe_mode = normalize_token(status_.safe_mode_summary);
  if (safe_mode.empty() || is_normal_safe_mode(safe_mode)) {
    return "[" + stage + "]";
  }

  return "[" + stage + " | " + safe_mode + "]";
}

std::string TuiStatusPanel::format_health_summary() const {
  const std::string health = normalize_token(status_.health_summary);
  const std::string safe_mode = normalize_token(status_.safe_mode_summary);

  if (health.empty()) {
    if (safe_mode.empty() || is_normal_safe_mode(safe_mode)) {
      return "degraded: unknown health";
    }
    return "degraded: unknown health; safe mode " + safe_mode;
  }

  if (is_healthy_status(health)) {
    if (safe_mode.empty() || is_normal_safe_mode(safe_mode)) {
      return "healthy";
    }
    return "healthy; safe mode " + safe_mode;
  }

  if (safe_mode.empty() || is_normal_safe_mode(safe_mode)) {
    return "degraded: " + health;
  }

  return "degraded: " + health + "; safe mode " + safe_mode;
}

std::string TuiStatusPanel::format_decision_summary() const {
  const std::string pending = normalize_token(status_.pending_interaction);
  if (!pending.empty()) {
    return "awaiting " + pending;
  }

  const std::string recovery = trim_copy(status_.recovery_summary);
  if (!recovery.empty()) {
    return recovery;
  }

  const std::string safe_mode = normalize_token(status_.safe_mode_summary);
  if (!safe_mode.empty() && !is_normal_safe_mode(safe_mode)) {
    return "safe mode " + safe_mode;
  }

  const std::string health = normalize_token(status_.health_summary);
  if (!health.empty() && !is_healthy_status(health)) {
    return "health " + health;
  }

  const std::string tool = trim_copy(status_.current_tool);
  if (!tool.empty()) {
    return "running " + tool;
  }

  const std::string stage = normalize_token(status_.stage);
  if (!stage.empty()) {
    return "stage " + stage;
  }

  return "degraded: decision summary unavailable";
}

}  // namespace dasall::tui::view