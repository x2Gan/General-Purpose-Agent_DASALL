#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "model/TuiScreenModel.h"
#include "terminal/FtxuiRendererAdapter.h"
#include "terminal/TuiTerminalCapabilityProbe.h"
#include "view/TuiComposer.h"
#include "view/TuiTextWidth.h"

namespace dasall::tui::manual_terminal {
namespace {

volatile std::sig_atomic_t resize_requested = 0;

void handle_resize_signal(int) { resize_requested = 1; }

struct TerminalSize {
  std::size_t columns = 80;
  std::size_t rows = 24;
};

struct ManualAnimationState {
  bool cursor_visible = true;
  int cursor_tick = 0;
  std::size_t spinner_index = 0;
};

struct PendingManualReceipt {
  bool active = false;
  std::string submitted_text;
  int ticks_remaining = 0;
};

[[nodiscard]] bool same_size(const TerminalSize& left,
                             const TerminalSize& right) noexcept {
  return left.columns == right.columns && left.rows == right.rows;
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }

  std::size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
    --last;
  }

  return std::string(text.substr(first, last - first));
}

[[nodiscard]] bool starts_with(std::string_view text,
                               std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string dots_spinner_frame(const std::size_t index) {
  switch (index % 3U) {
    case 0U:
      return "processing.";
    case 1U:
      return "processing..";
    default:
      return "processing...";
  }
}

[[nodiscard]] std::string now_timestamp() {
  const std::time_t now = std::time(nullptr);
  std::tm local_time{};
  if (localtime_r(&now, &local_time) == nullptr) {
    return "unknown-time";
  }

  char buffer[32]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local_time) == 0) {
    return "unknown-time";
  }
  return buffer;
}

[[nodiscard]] std::string startup_mode_to_string(
    terminal::TuiStartupMode startup_mode) {
  switch (startup_mode) {
    case terminal::TuiStartupMode::FullScreen:
      return "full";
    case terminal::TuiStartupMode::Narrow:
      return "narrow";
    case terminal::TuiStartupMode::Line:
      return "line";
    case terminal::TuiStartupMode::FailClosed:
      return "fail_closed";
  }

  return "fail_closed";
}

[[nodiscard]] std::string layout_mode_to_string(view::TuiLayoutMode layout_mode) {
  switch (layout_mode) {
    case view::TuiLayoutMode::FullScreen:
      return "full";
    case view::TuiLayoutMode::Narrow:
      return "narrow";
    case view::TuiLayoutMode::Line:
      return "line";
  }

  return "line";
}

[[nodiscard]] TerminalSize read_terminal_size() {
  for (const int file_descriptor : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
    winsize size{};
    if (::ioctl(file_descriptor, TIOCGWINSZ, &size) == 0 &&
        size.ws_col > 0U && size.ws_row > 0U) {
      return TerminalSize{static_cast<std::size_t>(size.ws_col),
                          static_cast<std::size_t>(size.ws_row)};
    }
  }

  return TerminalSize{};
}

void write_stdout(std::string_view text) {
  const char* cursor = text.data();
  std::size_t remaining = text.size();
  while (remaining > 0U) {
    const ssize_t written = ::write(STDOUT_FILENO, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    if (written == 0) {
      return;
    }
    cursor += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

[[nodiscard]] std::string encode_terminal_newlines(std::string_view text) {
  std::string encoded;
  encoded.reserve(text.size() + 16U);
  for (const char character : text) {
    if (character == '\n') {
      encoded += "\r\n";
    } else {
      encoded.push_back(character);
    }
  }
  return encoded;
}

[[nodiscard]] std::string_view redraw_control_prefix() noexcept {
  return "\x1b[?25l\x1b[H";
}

[[nodiscard]] std::string shell_quote_path(std::string_view path) {
  std::string quoted = "'";
  for (const char character : path) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(character);
    }
  }
  quoted += "'";
  return quoted;
}

[[nodiscard]] std::string resolve_editor_command() {
  for (const char* variable_name : {"VISUAL", "EDITOR"}) {
    const char* value = std::getenv(variable_name);
    if (value == nullptr) {
      continue;
    }
    const std::string normalized = trim_copy(value);
    if (!normalized.empty()) {
      return normalized;
    }
  }

  return {};
}

class TerminalSession {
 public:
  TerminalSession() = default;
  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;

  ~TerminalSession() { stop(); }

  [[nodiscard]] bool start(std::ostream& error_stream) {
    if (::tcgetattr(STDIN_FILENO, &original_termios_) != 0) {
      error_stream << "failed to read terminal attributes: " << std::strerror(errno)
                   << '\n';
      return false;
    }
    has_original_termios_ = true;
    enter_alternate_screen();
    return enable_raw(error_stream);
  }

  void stop() {
    disable_raw();
    leave_alternate_screen();
  }

  void suspend() {
    disable_raw();
    leave_alternate_screen();
  }

  [[nodiscard]] bool resume(std::ostream& error_stream) {
    enter_alternate_screen();
    return enable_raw(error_stream);
  }

 private:
  [[nodiscard]] bool enable_raw(std::ostream& error_stream) {
    if (!has_original_termios_) {
      return false;
    }

    termios raw = original_termios_;
    raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag &= static_cast<tcflag_t>(~(OPOST));
    raw.c_cflag |= CS8;
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
      error_stream << "failed to enable raw terminal mode: " << std::strerror(errno)
                   << '\n';
      return false;
    }
    raw_enabled_ = true;
    return true;
  }

  void disable_raw() {
    if (!raw_enabled_ || !has_original_termios_) {
      return;
    }
    static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios_));
    raw_enabled_ = false;
  }

  void enter_alternate_screen() {
    if (alternate_screen_active_) {
      return;
    }
    write_stdout("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
    alternate_screen_active_ = true;
  }

  void leave_alternate_screen() {
    if (!alternate_screen_active_) {
      return;
    }
    write_stdout("\x1b[?25h\x1b[?1049l");
    alternate_screen_active_ = false;
  }

  termios original_termios_{};
  bool has_original_termios_ = false;
  bool raw_enabled_ = false;
  bool alternate_screen_active_ = false;
};

class ResizeSignalGuard {
 public:
  ResizeSignalGuard() {
    struct sigaction action{};
    action.sa_handler = handle_resize_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    installed_ = ::sigaction(SIGWINCH, &action, &previous_action_) == 0;
  }

  ResizeSignalGuard(const ResizeSignalGuard&) = delete;
  ResizeSignalGuard& operator=(const ResizeSignalGuard&) = delete;

  ~ResizeSignalGuard() {
    if (installed_) {
      static_cast<void>(::sigaction(SIGWINCH, &previous_action_, nullptr));
    }
  }

 private:
  struct sigaction previous_action_{};
  bool installed_ = false;
};

[[nodiscard]] model::TuiMessageView make_message(std::string role,
                                                  std::string content,
                                                  std::vector<std::string> badges = {}) {
  model::TuiMessageView message;
  message.role = std::move(role);
  message.content = std::move(content);
  message.timestamp = now_timestamp();
  message.badges = std::move(badges);
  return message;
}

[[nodiscard]] model::TuiBanner make_banner(model::TuiBannerLevel level,
                                            std::string title,
                                            std::string message,
                                            std::string reason_code) {
  model::TuiBanner banner;
  banner.level = level;
  banner.title = std::move(title);
  banner.message = std::move(message);
  banner.reason_code = std::move(reason_code);
  banner.sticky = true;
  return banner;
}

[[nodiscard]] model::TuiScreenModel make_initial_model(
    const terminal::TuiTerminalCapabilities& capabilities,
    terminal::TuiStartupMode startup_mode,
    const view::TuiComposer& composer) {
  model::TuiScreenModel screen_model;
  screen_model.session.session_id = "manual-terminal-blk-tui-006";
  screen_model.session.profile_id = "local-manual";
  screen_model.session.daemon_readiness = "manual-evidence";
  screen_model.session.startup_mode = startup_mode_to_string(startup_mode);
  screen_model.session.started_at = now_timestamp();
  screen_model.status.stage = "ready";
  screen_model.status.budget_summary = "Manual evidence run";
  screen_model.status.health_summary = "terminal active";
  screen_model.status.safe_mode_summary = startup_mode_to_string(startup_mode);
  screen_model.route.current_provider_id = "manual";
  screen_model.route.current_model_id = "tui-terminal";
  screen_model.route.current_depth_tier = "local";
  screen_model.route.verification_state = "manual-only";
  screen_model.route.health = "ready";
  screen_model.route.next_preference.user_visible_summary = "auto";
  screen_model.route.next_preference.source = "BLK-TUI-006";
  screen_model.composer = composer.state();
  screen_model.focus = model::TuiFocusState::Composer;
  screen_model.debug_reason = "blk_tui_006_manual_terminal_started";

  screen_model.transcript.push_back(make_message(
      "system",
      "BLK-TUI-006 manual terminal ready. Enter submits, Ctrl-J inserts a newline, "
      "Up/Down recall history, Ctrl-R reverse-searches history, /editor opens VISUAL/EDITOR, "
      "/status and /session open checks, /exit quits.",
      {"manual", "composer"}));
  screen_model.transcript.push_back(make_message(
      "system",
      "CJK sample: 中文输入、かな、한글、emoji-less UTF-8 text should stay readable during "
      "IME commit and live resize.",
      {"cjk", "ime", "resize"}));

  for (const auto& issue : capabilities.issues) {
    if (!issue.blocking) {
      screen_model.banners.push_back(make_banner(model::TuiBannerLevel::Warning,
                                                 "Terminal degraded",
                                                 issue.message,
                                                 issue.reason_code));
    }
  }

  return screen_model;
}

void append_message(model::TuiScreenModel& screen_model,
                    std::string role,
                    std::string content,
                    std::vector<std::string> badges = {}) {
  screen_model.transcript.push_back(
      make_message(std::move(role), std::move(content), std::move(badges)));
  constexpr std::size_t kMaxTranscriptMessages = 80;
  if (screen_model.transcript.size() > kMaxTranscriptMessages) {
    screen_model.transcript.erase(screen_model.transcript.begin(),
                                  screen_model.transcript.begin() +
                                      static_cast<std::ptrdiff_t>(
                                          screen_model.transcript.size() -
                                          kMaxTranscriptMessages));
  }
}

void sync_composer(model::TuiScreenModel& screen_model,
                   const view::TuiComposer& composer,
                   std::string debug_reason) {
  const bool cursor_visible = screen_model.composer.cursor_visible;
  const std::string activity_indicator = screen_model.composer.activity_indicator;
  screen_model.composer = composer.state();
  screen_model.composer.cursor_visible = cursor_visible;
  screen_model.composer.activity_indicator = activity_indicator;
  if (screen_model.modal.kind == model::TuiModalKind::None) {
    screen_model.focus = model::TuiFocusState::Composer;
  }
  screen_model.debug_reason = std::move(debug_reason);
}

void apply_manual_visuals(model::TuiScreenModel& screen_model,
                          const ManualAnimationState& animation,
                          const PendingManualReceipt& pending_receipt) {
  screen_model.composer.cursor_visible = animation.cursor_visible;
  if (pending_receipt.active) {
    screen_model.composer.activity_indicator =
        dots_spinner_frame(animation.spinner_index);
  } else {
    screen_model.composer.activity_indicator.clear();
  }
}

void set_composer_text(view::TuiComposer& composer,
                       model::TuiScreenModel& screen_model,
                       std::string text,
                       std::string debug_reason,
                       std::optional<std::size_t> cursor_offset = std::nullopt) {
  const view::TuiComposerKeyEvent event{.key = view::TuiComposerKey::TextChanged,
                                        .text = std::move(text),
                                        .cursor_offset = cursor_offset};
  static_cast<void>(composer.handle_key(event));
  sync_composer(screen_model, composer, std::move(debug_reason));
}

void sync_pending_draft(view::TuiComposer& composer,
                        model::TuiScreenModel& screen_model,
                        const std::string& draft,
                        const std::size_t cursor_offset,
                        std::string debug_reason) {
  const std::size_t clamped_cursor = view::clamp_to_terminal_text_offset(
      draft,
      cursor_offset);
  if (draft == composer.state().text &&
      clamped_cursor == composer.state().cursor_offset) {
    return;
  }

  set_composer_text(composer,
                    screen_model,
                    draft,
                    std::move(debug_reason),
                    clamped_cursor);
}

void erase_previous_text_token(std::string& text, std::size_t& cursor_offset) {
  cursor_offset = view::clamp_to_terminal_text_offset(text, cursor_offset);
  if (cursor_offset == 0U) {
    return;
  }

  const std::size_t previous = view::previous_terminal_text_offset(text, cursor_offset);
  text.erase(previous, cursor_offset - previous);
  cursor_offset = previous;
}

void show_modal(model::TuiScreenModel& screen_model,
                model::TuiModalKind kind,
                std::string title,
                std::string body) {
  screen_model.modal.kind = kind;
  screen_model.modal.title = std::move(title);
  screen_model.modal.body = std::move(body);
  screen_model.modal.actions = {"Close"};
  screen_model.modal.selected_action_index = 0;
  screen_model.focus = model::TuiFocusState::Modal;
}

void hide_modal(model::TuiScreenModel& screen_model) {
  screen_model.modal = model::TuiModalState{};
  screen_model.focus = model::TuiFocusState::Composer;
}

void update_size_status(model::TuiScreenModel& screen_model,
                        const TerminalSize& size,
                        const terminal::FtxuiRendererAdapter& renderer) {
  const std::string layout_mode = layout_mode_to_string(
      renderer.apply_layout_metrics(size.columns, size.rows).mode);
  screen_model.session.startup_mode = layout_mode;

  std::ostringstream summary;
  summary << layout_mode << ' ' << size.columns << 'x'
          << size.rows;
  screen_model.status.safe_mode_summary = summary.str();
}

void redraw(const terminal::FtxuiRendererAdapter& renderer,
            const model::TuiScreenModel& screen_model,
            const TerminalSize& size) {
  const std::string rendered = renderer.render_to_screen(
      screen_model, size.columns, size.rows);
  write_stdout(redraw_control_prefix());
  const std::string terminal_rendered = encode_terminal_newlines(rendered);
  write_stdout(terminal_rendered);
  write_stdout("\x1b[H");
}

[[nodiscard]] std::optional<std::string> run_external_editor(
    TerminalSession& terminal_session,
    std::string_view seed_text,
    std::ostream& error_stream) {
  const std::string editor = resolve_editor_command();
  if (editor.empty()) {
    return std::nullopt;
  }

  char path_template[] = "/tmp/dasall-tui-editor-XXXXXX";
  const int file_descriptor = ::mkstemp(path_template);
  if (file_descriptor < 0) {
    return std::nullopt;
  }

  const ssize_t ignored = ::write(file_descriptor, seed_text.data(), seed_text.size());
  static_cast<void>(ignored);
  static_cast<void>(::close(file_descriptor));

  terminal_session.suspend();
  const std::string command = editor + ' ' + shell_quote_path(path_template);
  const int command_result = std::system(command.c_str());
  const bool resumed = terminal_session.resume(error_stream);
  if (!resumed || command_result != 0) {
    static_cast<void>(std::remove(path_template));
    return std::nullopt;
  }

  std::ifstream input(path_template, std::ios::binary);
  std::ostringstream edited;
  edited << input.rdbuf();
  static_cast<void>(std::remove(path_template));
  return edited.str();
}

void open_external_editor(view::TuiComposer& composer,
                          model::TuiScreenModel& screen_model,
                          TerminalSession& terminal_session,
                          const terminal::FtxuiRendererAdapter& renderer,
                          const TerminalSize& size,
                          std::ostream& error_stream) {
  const view::TuiComposerUpdate update = composer.open_external_editor();
  sync_composer(screen_model, composer, "external_editor_requested");
  redraw(renderer, screen_model, size);

  const std::optional<std::string> edited_text =
      run_external_editor(terminal_session, update.action.text, error_stream);
  if (!edited_text.has_value()) {
    screen_model.banners.push_back(make_banner(model::TuiBannerLevel::Warning,
                                               "External editor unavailable",
                                               "VISUAL/EDITOR was unset or the editor failed.",
                                               "external_editor_failed"));
  }

  static_cast<void>(composer.apply_external_editor_result(edited_text));
  sync_composer(screen_model, composer, "external_editor_completed");
}

void show_help(model::TuiScreenModel& screen_model) {
  show_modal(screen_model,
             model::TuiModalKind::Help,
             "Manual terminal help",
             "BLK-TUI-006 checks: resize the terminal between 120x36 and 80x24, "
             "enter CJK text through your IME, use Ctrl-J for multiline drafts, submit two "
             "turns then use Up/Down and Ctrl-R for history, and run /editor when VISUAL or "
             "EDITOR is configured. Press Esc or Enter to close this modal.");
}

[[nodiscard]] bool is_csi_cursor_sequence(std::string_view sequence,
                                          const char final_byte) noexcept {
  if (sequence.size() < 3U || sequence[0] != '\x1b' || sequence[1] != '[' ||
      sequence.back() != final_byte) {
    return false;
  }

  for (std::size_t index = 2U; index + 1U < sequence.size(); ++index) {
    const char byte = sequence[index];
    if ((byte < '0' || byte > '9') && byte != ';' && byte != '?') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_cursor_up_sequence(std::string_view sequence) noexcept {
  return sequence == "\x1bOA" || is_csi_cursor_sequence(sequence, 'A');
}

[[nodiscard]] bool is_cursor_down_sequence(std::string_view sequence) noexcept {
  return sequence == "\x1bOB" || is_csi_cursor_sequence(sequence, 'B');
}

[[nodiscard]] bool is_cursor_right_sequence(std::string_view sequence) noexcept {
  return sequence == "\x1bOC" || is_csi_cursor_sequence(sequence, 'C');
}

[[nodiscard]] bool is_cursor_left_sequence(std::string_view sequence) noexcept {
  return sequence == "\x1bOD" || is_csi_cursor_sequence(sequence, 'D');
}

[[nodiscard]] bool is_delete_sequence(std::string_view sequence) noexcept {
  if (sequence.size() < 4U || sequence[0] != '\x1b' || sequence[1] != '[' ||
      sequence.back() != '~' || sequence[2] != '3') {
    return false;
  }

  for (std::size_t index = 3U; index + 1U < sequence.size(); ++index) {
    const char byte = sequence[index];
    if ((byte < '0' || byte > '9') && byte != ';') {
      return false;
    }
  }
  return true;
}

void handle_submit(view::TuiComposer& composer,
                   model::TuiScreenModel& screen_model,
                   TerminalSession& terminal_session,
                   const terminal::FtxuiRendererAdapter& renderer,
                   const TerminalSize& size,
                   PendingManualReceipt& pending_receipt,
                   bool& should_exit,
                   std::ostream& error_stream) {
  const std::string draft = composer.state().text;
  const std::string command = trim_copy(draft);
  if (command == "/exit" || command == "/quit") {
    should_exit = true;
    return;
  }
  if (command == "/help") {
    set_composer_text(composer, screen_model, {}, "help_command");
    show_help(screen_model);
    return;
  }
  if (command == "/status") {
    set_composer_text(composer, screen_model, {}, "status_command");
    show_modal(screen_model,
               model::TuiModalKind::Session,
               "Manual status",
               "stage=" + screen_model.status.stage + "\nhealth=" +
                   screen_model.status.health_summary + "\nterminal=" +
                   screen_model.status.safe_mode_summary);
    return;
  }
  if (command == "/session") {
    set_composer_text(composer, screen_model, {}, "session_command");
    show_modal(screen_model,
               model::TuiModalKind::Session,
               "Manual session",
               "session_id=" + screen_model.session.session_id + "\nprofile_id=" +
                   screen_model.session.profile_id + "\nstartup_mode=" +
                   screen_model.session.startup_mode + "\nreadiness=" +
                   screen_model.session.daemon_readiness);
    return;
  }
  if (command == "/clear") {
    set_composer_text(composer, screen_model, {}, "clear_command");
    screen_model.transcript.clear();
    append_message(screen_model,
                   "system",
                   "Manual transcript cleared. Continue BLK-TUI-006 CJK/IME/resize checks.",
                   {"manual"});
    return;
  }
  if (command == "/editor" || starts_with(command, "/editor ")) {
    const std::string seed = starts_with(command, "/editor ")
                                 ? trim_copy(command.substr(std::string_view{"/editor"}.size()))
                                 : std::string{};
    set_composer_text(composer, screen_model, seed, "editor_seed");
    open_external_editor(composer,
                         screen_model,
                         terminal_session,
                         renderer,
                         size,
                         error_stream);
    return;
  }

  const view::TuiComposerUpdate update = composer.handle_key(
      view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Enter, .text = {}});
  sync_composer(screen_model, composer, "submit_requested");
  if (update.action.type != view::TuiComposerActionType::SubmitRequested) {
    return;
  }

  append_message(screen_model,
                 "user",
                 update.action.text,
                 {"submitted", "manual-input"});
  pending_receipt.active = true;
  pending_receipt.submitted_text = update.action.text;
  pending_receipt.ticks_remaining = 8;
  screen_model.status.stage = "waiting_interaction";
  screen_model.status.current_tool = "llm.local";
  screen_model.status.pending_interaction = "waiting for local model response";
  screen_model.status.health_summary = "awaiting local model response";
  static_cast<void>(composer.set_busy(true));
  sync_composer(screen_model, composer, "submit_pending_local_model");
}

void complete_pending_receipt(view::TuiComposer& composer,
                              model::TuiScreenModel& screen_model,
                              PendingManualReceipt& pending_receipt) {
  if (!pending_receipt.active) {
    return;
  }

  append_message(screen_model,
                 "assistant",
                 "Manual receipt captured locally for BLK-TUI-006. No runtime, memory, "
                 "recovery, or model-routing ownership is exercised by this harness.",
                 {"receipt", "local"});
  screen_model.status.stage = "ready";
  screen_model.status.current_tool.clear();
  screen_model.status.pending_interaction.clear();
  screen_model.status.health_summary = "manual receipt captured";
  static_cast<void>(composer.set_busy(false));
  pending_receipt = PendingManualReceipt{};
  sync_composer(screen_model, composer, "submit_completed");
}

void handle_escape_sequence(std::string_view sequence,
                            view::TuiComposer& composer,
                            model::TuiScreenModel& screen_model) {
  if (is_cursor_up_sequence(sequence)) {
    static_cast<void>(composer.handle_key(view::TuiComposerKeyEvent{
        .key = view::TuiComposerKey::Up,
        .text = {},
        .cursor_at_boundary = true,
        .draft_unmodified = true}));
    sync_composer(screen_model, composer, "history_older");
    return;
  }
  if (is_cursor_down_sequence(sequence)) {
    static_cast<void>(composer.handle_key(
        view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Down, .text = {}}));
    sync_composer(screen_model, composer, "history_newer");
    return;
  }
  if (is_cursor_left_sequence(sequence)) {
    static_cast<void>(composer.handle_key(
        view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Left, .text = {}}));
    sync_composer(screen_model, composer, "cursor_left");
    return;
  }
  if (is_cursor_right_sequence(sequence)) {
    static_cast<void>(composer.handle_key(
        view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Right, .text = {}}));
    sync_composer(screen_model, composer, "cursor_right");
    return;
  }
  if (is_delete_sequence(sequence)) {
    static_cast<void>(composer.handle_key(
        view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Delete, .text = {}}));
    sync_composer(screen_model, composer, "delete_at_cursor");
    return;
  }
  if (sequence == "\x1b\r" || sequence == "\x1b\n") {
    static_cast<void>(composer.handle_key(
      view::TuiComposerKeyEvent{.key = view::TuiComposerKey::AltEnter, .text = {}}));
    sync_composer(screen_model, composer, "alt_enter_newline");
    return;
  }

  if (screen_model.modal.kind != model::TuiModalKind::None) {
    hide_modal(screen_model);
    screen_model.debug_reason = "modal_hidden";
  }
}

[[nodiscard]] bool is_escape_sequence_final_byte(const unsigned char byte) noexcept {
  return byte >= 0x40U && byte <= 0x7EU;
}

[[nodiscard]] bool escape_sequence_complete(std::string_view sequence) noexcept {
  if (sequence.empty() || sequence.front() != '\x1b') {
    return true;
  }
  if (sequence.size() == 1U) {
    return false;
  }

  const char prefix = sequence[1];
  if (prefix == '[') {
    for (std::size_t index = 2U; index < sequence.size(); ++index) {
      if (is_escape_sequence_final_byte(static_cast<unsigned char>(sequence[index]))) {
        return true;
      }
    }
    return false;
  }
  if (prefix == 'O') {
    return sequence.size() >= 3U;
  }
  if (prefix == '\r' || prefix == '\n') {
    return true;
  }

  return true;
}

[[nodiscard]] std::size_t escape_sequence_length(std::string_view buffer) noexcept {
  if (buffer.empty() || buffer.front() != '\x1b') {
    return 0U;
  }
  if (buffer.size() == 1U) {
    return 1U;
  }

  const char prefix = buffer[1];
  if (prefix == '[') {
    for (std::size_t index = 2U; index < buffer.size(); ++index) {
      if (is_escape_sequence_final_byte(static_cast<unsigned char>(buffer[index]))) {
        return index + 1U;
      }
    }
    return buffer.size();
  }
  if (prefix == 'O') {
    return std::min<std::size_t>(3U, buffer.size());
  }
  if (prefix == '\r' || prefix == '\n') {
    return 2U;
  }

  return 1U;
}

void insert_printable_text(std::string& draft,
                           std::size_t& cursor_offset,
                           std::string_view bytes) {
  std::string printable;
  printable.reserve(bytes.size());
  for (const unsigned char byte : bytes) {
    if (byte >= 0x20U && byte != 0x7FU) {
      printable.push_back(static_cast<char>(byte));
    }
  }
  if (printable.empty()) {
    return;
  }

  cursor_offset = view::clamp_to_terminal_text_offset(draft, cursor_offset);
  draft.insert(cursor_offset, printable);
  cursor_offset += printable.size();
}

[[nodiscard]] std::string read_pending_escape_bytes() {
  std::string sequence = "\x1b";
  while (!escape_sequence_complete(sequence) && sequence.size() < 16U) {
    pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    if (::poll(&descriptor, 1, 15) <= 0 || (descriptor.revents & POLLIN) == 0) {
      break;
    }

    char byte{};
    const ssize_t bytes_read = ::read(STDIN_FILENO, &byte, 1U);
    if (bytes_read <= 0) {
      break;
    }
    sequence.push_back(byte);
  }
  return sequence;
}

void process_input_buffer(std::string_view buffer,
                          view::TuiComposer& composer,
                          model::TuiScreenModel& screen_model,
                          TerminalSession& terminal_session,
                          const terminal::FtxuiRendererAdapter& renderer,
                          const TerminalSize& size,
                          PendingManualReceipt& pending_receipt,
                          bool& should_exit,
                          std::ostream& error_stream) {
  std::string draft = composer.state().text;
  std::size_t cursor_offset = composer.state().cursor_offset;
  for (std::size_t offset = 0; offset < buffer.size(); ++offset) {
    const unsigned char byte = static_cast<unsigned char>(buffer[offset]);
    if (byte == 0x03U || byte == 0x04U) {
      should_exit = true;
      return;
    }
    if (byte == 0x1BU) {
      sync_pending_draft(composer,
                         screen_model,
                         draft,
                         cursor_offset,
                         "text_before_escape");
      std::string sequence = "\x1b";
      if (offset + 1U < buffer.size()) {
        const std::size_t sequence_length = escape_sequence_length(buffer.substr(offset));
        sequence = std::string(buffer.substr(offset, sequence_length));
        offset += sequence_length > 0U ? sequence_length - 1U : 0U;
      } else {
        sequence = read_pending_escape_bytes();
      }
      handle_escape_sequence(sequence, composer, screen_model);
      draft = composer.state().text;
      cursor_offset = composer.state().cursor_offset;
      continue;
    }
    if (byte == '\r') {
      sync_pending_draft(composer,
                         screen_model,
                         draft,
                         cursor_offset,
                         "text_before_submit");
      if (screen_model.modal.kind != model::TuiModalKind::None) {
        hide_modal(screen_model);
      } else {
        handle_submit(composer,
                      screen_model,
                      terminal_session,
                      renderer,
                      size,
                      pending_receipt,
                      should_exit,
                      error_stream);
      }
      draft = composer.state().text;
      cursor_offset = composer.state().cursor_offset;
      continue;
    }
    if (byte == '\n') {
      cursor_offset = view::clamp_to_terminal_text_offset(draft, cursor_offset);
      draft.insert(cursor_offset, "\n");
      ++cursor_offset;
      continue;
    }
    if (byte == 0x02U || byte == 0x06U) {
      sync_pending_draft(composer,
                         screen_model,
                         draft,
                         cursor_offset,
                         byte == 0x02U ? "text_before_ctrl_b" : "text_before_ctrl_f");
      static_cast<void>(composer.handle_key(view::TuiComposerKeyEvent{
          .key = byte == 0x02U ? view::TuiComposerKey::Left
                               : view::TuiComposerKey::Right,
          .text = {}}));
      sync_composer(screen_model,
                    composer,
                    byte == 0x02U ? "cursor_left" : "cursor_right");
      draft = composer.state().text;
      cursor_offset = composer.state().cursor_offset;
      continue;
    }
    if (byte == 0x12U) {
      sync_pending_draft(composer,
                         screen_model,
                         draft,
                         cursor_offset,
                         "text_before_reverse_search");
      static_cast<void>(composer.handle_key(
          view::TuiComposerKeyEvent{.key = view::TuiComposerKey::CtrlR, .text = {}}));
      sync_composer(screen_model, composer, "reverse_search");
      draft = composer.state().text;
      cursor_offset = composer.state().cursor_offset;
      continue;
    }
    if (byte == 0x7FU || byte == 0x08U) {
      erase_previous_text_token(draft, cursor_offset);
      continue;
    }

    insert_printable_text(draft,
                          cursor_offset,
                          std::string_view(buffer.data() + offset, 1U));
  }

  sync_pending_draft(composer, screen_model, draft, cursor_offset, "text_changed");
}

[[nodiscard]] terminal::TuiTerminalProbeEnvironment self_check_environment(
    int columns,
    int rows) {
  terminal::TuiTerminalProbeEnvironment environment;
  environment.stdin_is_tty = true;
  environment.stdout_is_tty = true;
  environment.stderr_is_tty = true;
  environment.term = "xterm-256color";
  environment.locale = "C.UTF-8";
  environment.columns = columns;
  environment.rows = rows;
  environment.utf8_enabled = true;
  environment.bracketed_paste_supported = true;
  environment.resize_events_supported = true;
  environment.external_editor_available = true;
  return environment;
}

[[nodiscard]] bool manual_history_recall_self_check(
    const terminal::TuiTerminalCapabilities& capabilities,
    terminal::TuiStartupMode startup_mode,
    const terminal::FtxuiRendererAdapter& renderer) {
  view::TuiComposer composer;
  model::TuiScreenModel screen_model = make_initial_model(
      capabilities,
      startup_mode,
      composer);
  TerminalSession terminal_session;
  PendingManualReceipt pending_receipt;
  bool should_exit = false;
  std::ostringstream error_stream;
  const TerminalSize size{120U, 36U};

  process_input_buffer("first prompt\r",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  complete_pending_receipt(composer, screen_model, pending_receipt);
  process_input_buffer("second prompt\r",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  complete_pending_receipt(composer, screen_model, pending_receipt);
  process_input_buffer("\x1b[A\x1b[A",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  if (should_exit || composer.state().text != "first prompt" ||
      composer.state().mode != "history-recall") {
    return false;
  }

  process_input_buffer("\x1bOB",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  if (should_exit || composer.state().text != "second prompt" ||
      composer.state().mode != "history-recall") {
    return false;
  }

  process_input_buffer("\x1b[1;2B",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  if (should_exit || !composer.state().text.empty() || composer.state().mode != "ready") {
    return false;
  }

  process_input_buffer("third prompt\r",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  complete_pending_receipt(composer, screen_model, pending_receipt);
  process_input_buffer("\x1b[1;2A",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  return !should_exit && composer.state().text == "third prompt" &&
      composer.state().mode == "history-recall";
}

[[nodiscard]] bool manual_pending_indicator_self_check(
    const terminal::TuiTerminalCapabilities& capabilities,
    terminal::TuiStartupMode startup_mode,
    const terminal::FtxuiRendererAdapter& renderer) {
  view::TuiComposer composer;
  model::TuiScreenModel screen_model = make_initial_model(
      capabilities,
      startup_mode,
      composer);
  TerminalSession terminal_session;
  PendingManualReceipt pending_receipt;
  ManualAnimationState animation;
  bool should_exit = false;
  std::ostringstream error_stream;
  const TerminalSize size{120U, 36U};

  process_input_buffer("prompt waiting for model\r",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  apply_manual_visuals(screen_model, animation, pending_receipt);
  const std::string pending_screen = renderer.render_to_screen(screen_model, 120, 36);
  if (should_exit || !pending_receipt.active ||
      composer.state().mode != "pending-interaction" ||
      screen_model.status.current_tool != "llm.local" ||
      pending_screen.find("processing.") == std::string::npos ||
      pending_screen.find("wait=") != std::string::npos ||
      pending_screen.find("[draft empty]|") != std::string::npos) {
    return false;
  }

  complete_pending_receipt(composer, screen_model, pending_receipt);
  apply_manual_visuals(screen_model, animation, pending_receipt);
  return !pending_receipt.active && composer.state().mode == "ready" &&
      screen_model.status.stage == "ready" &&
      screen_model.status.current_tool.empty() &&
      renderer.render_to_screen(screen_model, 120, 36)
              .find("Manual receipt captured locally") != std::string::npos;
}

[[nodiscard]] bool manual_simple_editing_self_check(
    const terminal::TuiTerminalCapabilities& capabilities,
    terminal::TuiStartupMode startup_mode,
    const terminal::FtxuiRendererAdapter& renderer) {
  view::TuiComposer composer;
  model::TuiScreenModel screen_model = make_initial_model(
      capabilities,
      startup_mode,
      composer);
  TerminalSession terminal_session;
  PendingManualReceipt pending_receipt;
  bool should_exit = false;
  std::ostringstream error_stream;
  const TerminalSize size{120U, 36U};

  process_input_buffer("abcd\x1b[D\x1b[DZ\x7f\x1b[3~",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  if (should_exit || composer.state().text != "abd" ||
      composer.state().cursor_offset != 2U) {
    return false;
  }

  process_input_buffer("\x1b[D\x1b[D\x1b[D中",
                       composer,
                       screen_model,
                       terminal_session,
                       renderer,
                       size,
                       pending_receipt,
                       should_exit,
                       error_stream);
  return !should_exit && composer.state().text == "中abd" &&
      composer.state().cursor_offset == std::string{"中"}.size();
}

[[nodiscard]] int run_self_check() {
  view::TuiComposer composer;
  terminal::TuiTerminalCapabilityProbe probe;
  const auto capabilities = probe.probe(self_check_environment(120, 36));
  const auto startup_mode = probe.select_startup_mode(capabilities);
  terminal::FtxuiRendererAdapter renderer;
  model::TuiScreenModel screen_model = make_initial_model(
      capabilities, startup_mode, composer);
  const std::string full_screen = renderer.render_to_screen(screen_model, 120, 36);
  const std::string narrow_screen = renderer.render_to_screen(screen_model, 80, 24);
  const std::string line_screen = renderer.render_to_screen(screen_model, 40, 12);
  const std::string terminal_full_screen = encode_terminal_newlines(full_screen);

  const auto has_expected_shape = [](std::string_view screen,
                                     const std::size_t expected_columns,
                                     const std::size_t expected_rows) {
    std::size_t rows = 0;
    std::size_t row_start = 0;
    while (row_start <= screen.size()) {
      const std::size_t row_end = screen.find('\n', row_start);
      const std::string_view line = row_end == std::string_view::npos
                                        ? screen.substr(row_start)
                                        : screen.substr(row_start, row_end - row_start);
      if (view::terminal_display_width(line) != expected_columns) {
        return false;
      }
      ++rows;
      if (row_end == std::string_view::npos) {
        break;
      }
      row_start = row_end + 1U;
    }
    return rows == expected_rows;
  };

  const bool ok = !full_screen.empty() && !narrow_screen.empty() &&
      full_screen.find("BLK-TUI-006") != std::string::npos &&
      full_screen.find("CJK sample") != std::string::npos &&
      narrow_screen.find("composer") != std::string::npos &&
      line_screen.find("composer") != std::string::npos &&
      has_expected_shape(full_screen, 120, 36) &&
      has_expected_shape(narrow_screen, 80, 24) &&
      has_expected_shape(line_screen, 40, 12) &&
      terminal_full_screen.find("\r\n") != std::string::npos &&
      redraw_control_prefix().find("\x1b[2J") == std::string_view::npos &&
      manual_history_recall_self_check(capabilities, startup_mode, renderer) &&
      manual_pending_indicator_self_check(capabilities, startup_mode, renderer) &&
      manual_simple_editing_self_check(capabilities, startup_mode, renderer);
  if (!ok) {
    std::cerr << "dasall_tui_manual_terminal self-check FAILED\n";
    return 1;
  }

  std::cout << "dasall_tui_manual_terminal self-check PASS\n";
  return 0;
}

[[nodiscard]] bool advance_manual_animation(ManualAnimationState& animation,
                                            PendingManualReceipt& pending_receipt) {
  bool changed = false;

  ++animation.cursor_tick;
  if (animation.cursor_tick >= 5) {
    animation.cursor_tick = 0;
    animation.cursor_visible = !animation.cursor_visible;
    changed = true;
  }

  if (pending_receipt.active) {
    animation.spinner_index = (animation.spinner_index + 1U) % 3U;
    if (pending_receipt.ticks_remaining > 0) {
      --pending_receipt.ticks_remaining;
    }
    changed = true;
  }

  return changed;
}

[[nodiscard]] int run_interactive_terminal() {
  terminal::TuiTerminalCapabilityProbe probe;
  const auto capabilities = probe.probe();
  const auto startup_mode = probe.select_startup_mode(capabilities);
  if (startup_mode == terminal::TuiStartupMode::FailClosed) {
    std::cerr << probe.format_startup_error(capabilities) << '\n';
    return 1;
  }

  TerminalSession terminal_session;
  if (!terminal_session.start(std::cerr)) {
    return 1;
  }
  ResizeSignalGuard resize_guard;

  view::TuiComposer composer;
  terminal::FtxuiRendererAdapter renderer;
  model::TuiScreenModel screen_model = make_initial_model(
      capabilities, startup_mode, composer);
  ManualAnimationState animation;
  PendingManualReceipt pending_receipt;
  bool should_exit = false;
  TerminalSize size = read_terminal_size();
  update_size_status(screen_model, size, renderer);
  apply_manual_visuals(screen_model, animation, pending_receipt);
  redraw(renderer, screen_model, size);

  while (!should_exit) {
    pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    const int poll_result = ::poll(&descriptor, 1, 100);
    const TerminalSize observed_size = read_terminal_size();
    if (resize_requested != 0 || !same_size(observed_size, size)) {
      resize_requested = 0;
      size = observed_size;
      update_size_status(screen_model, size, renderer);
      screen_model.debug_reason = "terminal_resized";
      apply_manual_visuals(screen_model, animation, pending_receipt);
      redraw(renderer, screen_model, size);
    }

    const bool animation_changed = advance_manual_animation(animation, pending_receipt);
    const bool pending_completed = pending_receipt.active &&
        pending_receipt.ticks_remaining <= 0;
    if (pending_completed) {
      complete_pending_receipt(composer, screen_model, pending_receipt);
      animation.cursor_visible = true;
      animation.cursor_tick = 0;
    }
    if (animation_changed || pending_completed) {
      apply_manual_visuals(screen_model, animation, pending_receipt);
      redraw(renderer, screen_model, size);
    }

    if (poll_result < 0 && errno == EINTR) {
      continue;
    }
    if (poll_result < 0) {
      break;
    }
    if (poll_result == 0 || (descriptor.revents & POLLIN) == 0) {
      continue;
    }

    char input_buffer[64]{};
    const ssize_t bytes_read = ::read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
    if (bytes_read <= 0) {
      continue;
    }
    process_input_buffer(std::string_view(input_buffer, static_cast<std::size_t>(bytes_read)),
                         composer,
                         screen_model,
                         terminal_session,
                         renderer,
                         size,
                         pending_receipt,
                         should_exit,
                         std::cerr);
    apply_manual_visuals(screen_model, animation, pending_receipt);
    redraw(renderer, screen_model, size);
  }

  return 0;
}

}  // namespace
}  // namespace dasall::tui::manual_terminal

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--self-check") {
    return dasall::tui::manual_terminal::run_self_check();
  }
  if (argc > 1 && std::string_view(argv[1]) == "--help") {
    std::cout << "Usage: dasall_tui_manual_terminal [--self-check]\n"
              << "Interactive mode requires a real TTY. Use /help inside the TUI for "
                 "BLK-TUI-006 checks.\n";
    return 0;
  }
  return dasall::tui::manual_terminal::run_interactive_terminal();
}