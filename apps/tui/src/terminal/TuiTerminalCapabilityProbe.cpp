#include "terminal/TuiTerminalCapabilityProbe.h"

#include <cstdlib>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include <sys/ioctl.h>
#include <unistd.h>

namespace dasall::tui::terminal {
namespace {

[[nodiscard]] std::string trim_copy(const std::string_view& text) {
  std::size_t first = 0;
  while (first < text.size()) {
    const unsigned char character = static_cast<unsigned char>(text[first]);
    if (!std::isspace(character)) {
      break;
    }
    ++first;
  }

  std::size_t last = text.size();
  while (last > first) {
    const unsigned char character = static_cast<unsigned char>(text[last - 1]);
    if (!std::isspace(character)) {
      break;
    }
    --last;
  }

  return std::string(text.substr(first, last - first));
}

[[nodiscard]] std::string lowercase_copy(const std::string_view& text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char character : text) {
    lowered.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return lowered;
}

[[nodiscard]] std::string read_environment_value(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return {};
  }

  return trim_copy(value);
}

[[nodiscard]] std::optional<int> parse_positive_integer(const std::string_view& text) {
  const std::string normalized = trim_copy(text);
  if (normalized.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const long parsed = std::strtol(normalized.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || parsed <= 0L) {
    return std::nullopt;
  }

  return static_cast<int>(parsed);
}

[[nodiscard]] bool locale_supports_utf8(const std::string_view& locale) {
  const std::string normalized = lowercase_copy(locale);
  return normalized.find("utf-8") != std::string::npos ||
         normalized.find("utf8") != std::string::npos;
}

[[nodiscard]] bool terminal_kind_supported(const std::string_view& term) {
  const std::string normalized = lowercase_copy(trim_copy(term));
  return !normalized.empty() && normalized != "dumb" && normalized != "unknown";
}

[[nodiscard]] bool has_external_editor_command(
    const std::string_view& visual,
    const std::string_view& editor) {
  return !trim_copy(visual).empty() || !trim_copy(editor).empty();
}

[[nodiscard]] std::string resolve_locale() {
  for (const char* variable_name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
    const std::string value = read_environment_value(variable_name);
    if (!value.empty()) {
      return value;
    }
  }

  return {};
}

[[nodiscard]] std::optional<std::pair<int, int>> read_terminal_size_from_fd(int fd) {
  winsize size{};
  if (::ioctl(fd, TIOCGWINSZ, &size) != 0) {
    return std::nullopt;
  }

  if (size.ws_col == 0U || size.ws_row == 0U) {
    return std::nullopt;
  }

  return std::pair<int, int>{static_cast<int>(size.ws_col), static_cast<int>(size.ws_row)};
}

[[nodiscard]] std::optional<std::pair<int, int>> resolve_terminal_size() {
  for (const int fd : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
    const auto size = read_terminal_size_from_fd(fd);
    if (size.has_value()) {
      return size;
    }
  }

  const auto columns = parse_positive_integer(read_environment_value("COLUMNS"));
  const auto rows = parse_positive_integer(read_environment_value("LINES"));
  if (!columns.has_value() || !rows.has_value()) {
    return std::nullopt;
  }

  return std::pair<int, int>{*columns, *rows};
}

[[nodiscard]] TuiStartupIssue make_issue(
    std::string reason_code,
    std::string message,
    bool blocking) {
  TuiStartupIssue issue;
  issue.reason_code = std::move(reason_code);
  issue.message = std::move(message);
  issue.blocking = blocking;
  return issue;
}

}  // namespace

bool TuiTerminalCapabilities::has_blocking_issue() const noexcept {
  for (const auto& issue : issues) {
    if (issue.blocking) {
      return true;
    }
  }

  return false;
}

TuiTerminalCapabilityProbe::TuiTerminalCapabilityProbe(TuiTerminalProbeThresholds thresholds)
    : thresholds_(std::move(thresholds)) {}

TuiTerminalCapabilities TuiTerminalCapabilityProbe::probe() const {
  TuiTerminalProbeEnvironment environment;
  environment.stdin_is_tty = ::isatty(STDIN_FILENO) == 1;
  environment.stdout_is_tty = ::isatty(STDOUT_FILENO) == 1;
  environment.stderr_is_tty = ::isatty(STDERR_FILENO) == 1;
  environment.term = read_environment_value("TERM");
  environment.locale = resolve_locale();

  const auto terminal_size = resolve_terminal_size();
  if (terminal_size.has_value()) {
    environment.columns = terminal_size->first;
    environment.rows = terminal_size->second;
  }

  environment.utf8_enabled = locale_supports_utf8(environment.locale);

  const bool interactive_output =
      environment.stdout_is_tty && environment.stderr_is_tty;
  const bool term_supported = terminal_kind_supported(environment.term);
  environment.bracketed_paste_supported =
      environment.stdin_is_tty && interactive_output && term_supported;
  environment.resize_events_supported = interactive_output && term_supported &&
      environment.columns.has_value() && environment.rows.has_value();
  environment.external_editor_available = has_external_editor_command(
      read_environment_value("VISUAL"),
      read_environment_value("EDITOR"));

  return probe(environment);
}

TuiTerminalCapabilities TuiTerminalCapabilityProbe::probe(
    const TuiTerminalProbeEnvironment& environment) const {
  TuiTerminalCapabilities capabilities;
  capabilities.stdin_is_tty = environment.stdin_is_tty;
  capabilities.stdout_is_tty = environment.stdout_is_tty;
  capabilities.stderr_is_tty = environment.stderr_is_tty;
  capabilities.term = trim_copy(environment.term);
  capabilities.locale = trim_copy(environment.locale);
  capabilities.columns = environment.columns.value_or(0);
  capabilities.rows = environment.rows.value_or(0);
  capabilities.utf8_enabled = environment.utf8_enabled;
  capabilities.bracketed_paste_supported = environment.bracketed_paste_supported;
  capabilities.resize_events_supported = environment.resize_events_supported;
  capabilities.external_editor_available = environment.external_editor_available;

  if (!capabilities.stdin_is_tty) {
    capabilities.issues.push_back(make_issue(
        "non_tty_stdin",
        "stdin is not attached to a TTY. Use dasall-cli for non-interactive control-plane tasks.",
        true));
  }

  if (!capabilities.stdout_is_tty) {
    capabilities.issues.push_back(make_issue(
        "non_tty_stdout",
        "stdout is not attached to a TTY. Use dasall-cli for non-interactive control-plane tasks.",
        true));
  }

  if (!capabilities.stderr_is_tty) {
    capabilities.issues.push_back(make_issue(
        "non_tty_stderr",
        "stderr is not attached to a TTY. Use dasall-cli for non-interactive control-plane tasks.",
        true));
  }

  if (!terminal_kind_supported(capabilities.term)) {
    capabilities.issues.push_back(make_issue(
        "invalid_term",
        "TERM is empty, dumb, or unsupported for TUI startup.",
        true));
  }

  if (capabilities.columns > 0 && capabilities.rows > 0 &&
      !meets_size_floor(capabilities.columns,
                        capabilities.rows,
                        thresholds_.line_min_columns,
                        thresholds_.line_min_rows)) {
    std::ostringstream message;
    message << "terminal size " << capabilities.columns << 'x' << capabilities.rows
            << " is below the " << thresholds_.line_min_columns << 'x'
            << thresholds_.line_min_rows << " minimum for TUI startup.";
    capabilities.issues.push_back(
        make_issue("terminal_too_small", message.str(), true));
  }

  if (!capabilities.utf8_enabled) {
    capabilities.issues.push_back(make_issue(
        "utf8_unavailable",
        "UTF-8 locale is unavailable; startup will fall back to line mode.",
        false));
  }

  if (!capabilities.bracketed_paste_supported) {
    capabilities.issues.push_back(make_issue(
        "bracketed_paste_unavailable",
        "bracketed paste is unavailable; startup will fall back to line mode.",
        false));
  }

  if (!capabilities.resize_events_supported) {
    capabilities.issues.push_back(make_issue(
        "resize_unavailable",
        "resize awareness is unavailable; startup will fall back to line mode.",
        false));
  }

  if (!capabilities.external_editor_available) {
    capabilities.issues.push_back(make_issue(
        "external_editor_unavailable",
        "VISUAL and EDITOR are unset; /editor will remain disabled.",
        false));
  }

  return capabilities;
}

TuiStartupMode TuiTerminalCapabilityProbe::select_startup_mode(
    const TuiTerminalCapabilities& capabilities) const {
  if (capabilities.has_blocking_issue()) {
    return TuiStartupMode::FailClosed;
  }

  const bool advanced_input_ready = capabilities.utf8_enabled &&
      capabilities.bracketed_paste_supported && capabilities.resize_events_supported;
  if (advanced_input_ready &&
      meets_size_floor(capabilities.columns,
                       capabilities.rows,
                       thresholds_.full_min_columns,
                       thresholds_.full_min_rows)) {
    return TuiStartupMode::FullScreen;
  }

  if (advanced_input_ready &&
      meets_size_floor(capabilities.columns,
                       capabilities.rows,
                       thresholds_.narrow_min_columns,
                       thresholds_.narrow_min_rows)) {
    return TuiStartupMode::Narrow;
  }

  return TuiStartupMode::Line;
}

std::string TuiTerminalCapabilityProbe::format_startup_error(
    const TuiTerminalCapabilities& capabilities) const {
  if (select_startup_mode(capabilities) != TuiStartupMode::FailClosed) {
    return {};
  }

  std::ostringstream message;
  message << "TUI startup blocked:";

  bool appended_issue = false;
  for (const auto& issue : capabilities.issues) {
    if (!issue.blocking || !issue.has_reason()) {
      continue;
    }

    message << (appended_issue ? ' ' : ' ') << issue.message;
    appended_issue = true;
  }

  if (!appended_issue) {
    message << " terminal capability requirements are not satisfied.";
  }

  return message.str();
}

bool TuiTerminalCapabilityProbe::meets_size_floor(
    int columns,
    int rows,
    int min_columns,
    int min_rows) const noexcept {
  return columns >= min_columns && rows >= min_rows;
}

}  // namespace dasall::tui::terminal