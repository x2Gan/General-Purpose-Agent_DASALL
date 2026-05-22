#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::tui::terminal {

enum class TuiStartupMode {
  FullScreen,
  Narrow,
  Line,
  FailClosed,
};

struct TuiStartupIssue {
  std::string reason_code;
  std::string message;
  bool blocking = false;

  [[nodiscard]] bool has_reason() const noexcept {
    return !reason_code.empty() && !message.empty();
  }
};

struct TuiTerminalCapabilities {
  bool stdin_is_tty = false;
  bool stdout_is_tty = false;
  bool stderr_is_tty = false;
  std::string term;
  std::string locale;
  int columns = 0;
  int rows = 0;
  bool utf8_enabled = false;
  bool bracketed_paste_supported = false;
  bool resize_events_supported = false;
  bool external_editor_available = false;
  std::vector<TuiStartupIssue> issues;

  [[nodiscard]] bool has_blocking_issue() const noexcept;
};

struct TuiTerminalProbeEnvironment {
  bool stdin_is_tty = false;
  bool stdout_is_tty = false;
  bool stderr_is_tty = false;
  std::string term;
  std::string locale;
  std::optional<int> columns;
  std::optional<int> rows;
  bool utf8_enabled = false;
  bool bracketed_paste_supported = false;
  bool resize_events_supported = false;
  bool external_editor_available = false;
};

struct TuiTerminalProbeThresholds {
  int full_min_columns = 120;
  int full_min_rows = 36;
  int narrow_min_columns = 80;
  int narrow_min_rows = 24;
  int line_min_columns = 40;
  int line_min_rows = 12;
};

class TuiTerminalCapabilityProbe {
 public:
  explicit TuiTerminalCapabilityProbe(TuiTerminalProbeThresholds thresholds = {});

  [[nodiscard]] TuiTerminalCapabilities probe() const;
  [[nodiscard]] TuiTerminalCapabilities probe(
      const TuiTerminalProbeEnvironment& environment) const;
  [[nodiscard]] TuiStartupMode select_startup_mode(
      const TuiTerminalCapabilities& capabilities) const;
  [[nodiscard]] std::string format_startup_error(
      const TuiTerminalCapabilities& capabilities) const;

 private:
  [[nodiscard]] bool meets_size_floor(
      int columns,
      int rows,
      int min_columns,
      int min_rows) const noexcept;

  TuiTerminalProbeThresholds thresholds_;
};

}  // namespace dasall::tui::terminal