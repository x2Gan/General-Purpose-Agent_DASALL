#include "app/TuiApp.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>

#if !defined(_WIN32)
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "command/TuiSlashCommandParser.h"
#include "model/TuiReducer.h"
#include "view/TuiTextWidth.h"
#include "view/TuiTranscriptView.h"

namespace dasall::tui::app {
namespace {

using dasall::tui::model::TuiAction;
using dasall::tui::model::TuiActionType;
using dasall::tui::model::TuiBanner;
using dasall::tui::model::TuiBannerLevel;
using dasall::tui::model::TuiModalKind;
using dasall::tui::model::TuiModalState;

#if !defined(_WIN32)

volatile std::sig_atomic_t interactive_resize_requested = 0;

void handle_interactive_resize_signal(int) { interactive_resize_requested = 1; }

struct InteractiveTerminalSize {
  std::size_t columns = 80;
  std::size_t rows = 24;
};

[[nodiscard]] bool same_interactive_size(
    const InteractiveTerminalSize& left,
    const InteractiveTerminalSize& right) noexcept {
  return left.columns == right.columns && left.rows == right.rows;
}

[[nodiscard]] InteractiveTerminalSize read_interactive_terminal_size() {
  for (const int file_descriptor : {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO}) {
    winsize size{};
    if (::ioctl(file_descriptor, TIOCGWINSZ, &size) == 0 &&
        size.ws_col > 0U && size.ws_row > 0U) {
      return InteractiveTerminalSize{static_cast<std::size_t>(size.ws_col),
                                     static_cast<std::size_t>(size.ws_row)};
    }
  }

  return InteractiveTerminalSize{};
}

void write_interactive_stdout(std::string_view text) {
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

[[nodiscard]] std::string encode_interactive_newlines(std::string_view text) {
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

[[nodiscard]] std::string_view interactive_redraw_prefix() noexcept {
  return "\x1b[?25l\x1b[H";
}

class InteractiveTerminalSession {
 public:
  InteractiveTerminalSession() = default;
  InteractiveTerminalSession(const InteractiveTerminalSession&) = delete;
  InteractiveTerminalSession& operator=(const InteractiveTerminalSession&) = delete;

  ~InteractiveTerminalSession() { stop(); }

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
    write_interactive_stdout("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
    alternate_screen_active_ = true;
  }

  void leave_alternate_screen() {
    if (!alternate_screen_active_) {
      return;
    }
    write_interactive_stdout("\x1b[?25h\x1b[?1049l");
    alternate_screen_active_ = false;
  }

  termios original_termios_{};
  bool has_original_termios_ = false;
  bool raw_enabled_ = false;
  bool alternate_screen_active_ = false;
};

class InteractiveResizeSignalGuard {
 public:
  InteractiveResizeSignalGuard() {
    struct sigaction action{};
    action.sa_handler = handle_interactive_resize_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    installed_ = ::sigaction(SIGWINCH, &action, &previous_action_) == 0;
  }

  InteractiveResizeSignalGuard(const InteractiveResizeSignalGuard&) = delete;
  InteractiveResizeSignalGuard& operator=(const InteractiveResizeSignalGuard&) = delete;

  ~InteractiveResizeSignalGuard() {
    if (installed_) {
      static_cast<void>(::sigaction(SIGWINCH, &previous_action_, nullptr));
    }
  }

 private:
  struct sigaction previous_action_{};
  bool installed_ = false;
};

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

[[nodiscard]] bool is_csi_tilde_sequence(std::string_view sequence,
                                         const char leading_digit) noexcept {
  if (sequence.size() < 4U || sequence[0] != '\x1b' || sequence[1] != '[' ||
      sequence[2] != leading_digit || sequence.back() != '~') {
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

[[nodiscard]] bool is_page_up_sequence(std::string_view sequence) noexcept {
  return is_csi_tilde_sequence(sequence, '5');
}

[[nodiscard]] bool is_page_down_sequence(std::string_view sequence) noexcept {
  return is_csi_tilde_sequence(sequence, '6');
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

void erase_previous_text_token(std::string& text, std::size_t& cursor_offset) {
  cursor_offset = view::clamp_to_terminal_text_offset(text, cursor_offset);
  if (cursor_offset == 0U) {
    return;
  }

  const std::size_t previous = view::previous_terminal_text_offset(text, cursor_offset);
  text.erase(previous, cursor_offset - previous);
  cursor_offset = previous;
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

#endif

void apply_reduced_action(model::TuiScreenModel& screen_model,
                          const model::TuiAction& action) {
  screen_model = model::reduce(std::move(screen_model), action);
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  std::size_t end = text.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return std::string(text.substr(start, end - start));
}

[[nodiscard]] std::size_t saturating_sub_size(const std::size_t left,
                                              const std::size_t right) noexcept {
  return left > right ? left - right : 0U;
}

[[nodiscard]] std::size_t transcript_max_scroll_offset(
    const std::size_t total_line_count,
    const std::size_t viewport_height) noexcept {
  if (viewport_height == 0 || total_line_count <= viewport_height) {
    return 0;
  }

  return total_line_count - viewport_height;
}

[[nodiscard]] std::string session_modal_body(
    const data::TuiSessionView& session,
    const data::TuiModelRouteProjection& route) {
  std::ostringstream body;
  body << "session_id: " << session.session_id << '\n'
       << "profile_id: " << session.profile_id << '\n'
       << "startup_mode: " << session.startup_mode << '\n'
       << "readiness: " << session.daemon_readiness << '\n'
       << "route: " << route.current_provider_id << "/" << route.current_model_id;
  return body.str();
}

[[nodiscard]] std::string status_summary_message(
    const data::TuiStatusProjection& status) {
  std::ostringstream message;
  message << "stage=" << (status.stage.empty() ? "unknown" : trim_copy(status.stage))
          << ", tool="
          << (status.current_tool.empty() ? "none" : trim_copy(status.current_tool))
          << ", health="
          << (status.health_summary.empty() ? "unknown" : trim_copy(status.health_summary));
  return message.str();
}

[[nodiscard]] model::TuiAction make_banner_action(
    const model::TuiBannerLevel level,
    std::string title,
    std::string message,
    std::string reason_code,
    std::string debug_reason,
    const bool sticky = false) {
  TuiAction action;
  action.type = TuiActionType::BannerAdded;
  action.debug_reason = std::move(debug_reason);

  TuiBanner banner;
  banner.level = level;
  banner.title = std::move(title);
  banner.message = std::move(message);
  if (!reason_code.empty()) {
    banner.reason_code = std::move(reason_code);
  }
  banner.sticky = sticky;
  action.banner = std::move(banner);
  return action;
}

[[nodiscard]] model::TuiAction make_modal_action(
    model::TuiModalState modal,
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::ModalShown;
  action.debug_reason = std::move(debug_reason);
  action.modal = std::move(modal);
  return action;
}

[[nodiscard]] model::TuiAction make_route_action(
    data::TuiModelRouteProjection route,
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::RouteUpdated;
  action.debug_reason = std::move(debug_reason);
  action.route = std::move(route);
  return action;
}

[[nodiscard]] model::TuiAction make_transcript_message_action(
    std::string role,
    std::string content,
    std::string timestamp,
    std::vector<std::string> badges,
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::TranscriptMessageAppended;
  action.debug_reason = std::move(debug_reason);
  action.transcript_role = std::move(role);
  action.transcript_content = std::move(content);
  action.transcript_timestamp = std::move(timestamp);
  action.transcript_badges = std::move(badges);
  return action;
}

[[nodiscard]] model::TuiAction make_foreground_session_reset_action(
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::ForegroundSessionResetApplied;
  action.debug_reason = std::move(debug_reason);
  return action;
}

[[nodiscard]] data::TuiStatusProjection make_baseline_status(
    std::string startup_mode) {
  data::TuiStatusProjection status;
  status.stage = "ready";
  status.current_tool = "";
  status.pending_interaction = "";
  status.budget_summary = "Budget 100% remaining";
  status.recovery_summary = "";
  status.health_summary = "healthy";
  status.safe_mode_summary = startup_mode == "line" ? "line" : "normal";
  return status;
}

[[nodiscard]] bool startup_issue_means_daemon_unavailable(
    const data::TuiDataSourceIssue& issue) {
  return issue.reason_code == "socket_missing" ||
         issue.reason_code == "daemon_unavailable";
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

[[nodiscard]] bool composer_waiting_for_response(
    const model::TuiComposerState& composer) noexcept {
  return composer.mode == "submitting" || composer.mode == "pending-interaction";
}

}  // namespace

TuiApp::TuiApp() = default;

TuiApp::TuiApp(terminal::TuiTerminalCapabilityProbe probe)
    : probe_(std::move(probe)) {}

int TuiApp::run(TuiAppOptions options) {
  initialize_components(options);

  if (data_source_ == nullptr) {
    if (output_stream_ != nullptr && !last_error_.empty()) {
      *output_stream_ << last_error_ << '\n';
    }
    shutdown_clean_ = false;
    return 1;
  }

  terminal_capabilities_ = options.probe_environment.has_value()
                               ? probe_.probe(*options.probe_environment)
                               : probe_.probe();
  startup_mode_ = probe_.select_startup_mode(terminal_capabilities_);
  terminal_width_ = options.terminal_width;
  terminal_height_ = options.terminal_height;

  if (startup_mode_ == terminal::TuiStartupMode::FailClosed) {
    last_error_ = probe_.format_startup_error(terminal_capabilities_);
    if (output_stream_ != nullptr && !last_error_.empty()) {
      *output_stream_ << last_error_ << '\n';
    }
    shutdown_clean_ = false;
    return 1;
  }

  if (!open_session()) {
    emit_startup_error();
    const int exit_code = shutdown();
    return exit_code == 0 ? 1 : exit_code;
  }

  if (!load_route_catalog()) {
    emit_startup_error();
    const int exit_code = shutdown();
    return exit_code == 0 ? 1 : exit_code;
  }

  sync_composer_state();
  for (std::size_t tick_index = 0; tick_index < options.bootstrap_tick_count; ++tick_index) {
    if (!tick()) {
      break;
    }
  }

  for (const auto& action : options.scripted_actions) {
    dispatch_action(action);
    if (exit_requested_) {
      break;
    }
  }

  if (!exit_requested_) {
    for (std::size_t tick_index = 0; tick_index < options.post_action_tick_count;
         ++tick_index) {
      if (!tick()) {
        break;
      }
    }
  }

  if (!exit_requested_ && options.selector_preview_mode.has_value()) {
    show_selector_preview(*options.selector_preview_mode);
  }

  if (options.interactive_session) {
    return run_interactive_session();
  }

  render_current_screen(options.print_final_screen);
  return shutdown();
}

int TuiApp::run_interactive_session() {
#if defined(_WIN32)
  render_current_screen(true);
  return shutdown();
#else
  InteractiveTerminalSession terminal_session;
  if (!terminal_session.start(std::cerr)) {
    shutdown_close_reason_ = "interactive_terminal_start_failed";
    return shutdown();
  }

  InteractiveResizeSignalGuard resize_guard;
  InteractiveTerminalSize terminal_size = read_interactive_terminal_size();
  terminal_width_ = terminal_size.columns;
  terminal_height_ = terminal_size.rows;

  auto redraw = [this]() {
    render_current_screen(false);
    write_interactive_stdout(interactive_redraw_prefix());
    write_interactive_stdout(encode_interactive_newlines(last_rendered_screen_));
    write_interactive_stdout("\x1b[H");
  };
  interactive_redraw_hook_ = redraw;

  redraw();

  while (!exit_requested_) {
    const InteractiveTerminalSize observed_size = read_interactive_terminal_size();
    if (interactive_resize_requested != 0 ||
        !same_interactive_size(observed_size, terminal_size)) {
      interactive_resize_requested = 0;
      terminal_size = observed_size;
      terminal_width_ = terminal_size.columns;
      terminal_height_ = terminal_size.rows;
      redraw();
    }

    while (tick()) {
      redraw();
    }

    if (advance_interactive_animation()) {
      redraw();
    }

    pollfd descriptor{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    const int poll_result = ::poll(&descriptor, 1, 100);
    if (poll_result < 0 && errno == EINTR) {
      continue;
    }
    if (poll_result < 0) {
      shutdown_close_reason_ = "interactive_terminal_poll_failed";
      break;
    }
    if (poll_result == 0) {
      continue;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP)) != 0 &&
        (descriptor.revents & POLLIN) == 0) {
      shutdown_close_reason_ = "interactive_terminal_closed";
      break;
    }
    if ((descriptor.revents & POLLIN) == 0) {
      continue;
    }

    char input_buffer[64]{};
    const ssize_t bytes_read = ::read(STDIN_FILENO, input_buffer, sizeof(input_buffer));
    if (bytes_read <= 0) {
      continue;
    }

    std::string draft = composer_.state().text;
    std::size_t cursor_offset = composer_.state().cursor_offset;

    for (std::size_t offset = 0; offset < static_cast<std::size_t>(bytes_read); ++offset) {
      const unsigned char byte = static_cast<unsigned char>(input_buffer[offset]);
      if (byte == 0x03U || byte == 0x04U) {
        shutdown_close_reason_ = "interactive_terminal_exit";
        exit_requested_ = true;
        break;
      }

      if (byte == 0x1BU) {
        sync_pending_draft(draft, cursor_offset, "interactive_text_before_escape");
        std::string sequence = "\x1b";
        if (offset + 1U < static_cast<std::size_t>(bytes_read)) {
          const std::size_t sequence_length =
              escape_sequence_length(std::string_view(input_buffer + offset,
                                                      static_cast<std::size_t>(bytes_read) -
                                                          offset));
          sequence.assign(input_buffer + offset, sequence_length);
          offset += sequence_length > 0U ? sequence_length - 1U : 0U;
        } else {
          sequence = read_pending_escape_bytes();
        }

        if (is_cursor_up_sequence(sequence)) {
          static_cast<void>(composer_.handle_key(view::TuiComposerKeyEvent{
              .key = view::TuiComposerKey::Up,
              .text = {},
              .cursor_at_boundary = true,
              .draft_unmodified = true}));
          sync_composer_state();
        } else if (is_cursor_down_sequence(sequence)) {
          static_cast<void>(composer_.handle_key(
              view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Down, .text = {}}));
          sync_composer_state();
        } else if (is_cursor_left_sequence(sequence)) {
          static_cast<void>(composer_.handle_key(
              view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Left, .text = {}}));
          sync_composer_state();
        } else if (is_cursor_right_sequence(sequence)) {
          static_cast<void>(composer_.handle_key(
              view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Right, .text = {}}));
          sync_composer_state();
        } else if (is_delete_sequence(sequence)) {
          static_cast<void>(composer_.handle_key(
              view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Delete, .text = {}}));
          sync_composer_state();
        } else if (is_page_up_sequence(sequence)) {
          scroll_transcript_page(-1);
        } else if (is_page_down_sequence(sequence)) {
          scroll_transcript_page(1);
        } else if (sequence == "\x1b\r" || sequence == "\x1b\n") {
          static_cast<void>(composer_.handle_key(
              view::TuiComposerKeyEvent{.key = view::TuiComposerKey::AltEnter, .text = {}}));
          sync_composer_state();
        } else if (screen_model_.modal.kind != model::TuiModalKind::None) {
          hide_modal("interactive_modal_hidden");
        }

        draft = composer_.state().text;
        cursor_offset = composer_.state().cursor_offset;
        continue;
      }

      if (byte == '\r') {
        sync_pending_draft(draft, cursor_offset, "interactive_text_before_submit");
        if (screen_model_.modal.kind != model::TuiModalKind::None) {
          hide_modal("interactive_modal_hidden");
        } else {
          handle_interactive_submit();
        }
        draft = composer_.state().text;
        cursor_offset = composer_.state().cursor_offset;
        continue;
      }

      if (byte == '\n') {
        cursor_offset = view::clamp_to_terminal_text_offset(draft, cursor_offset);
        draft.insert(cursor_offset, "\n");
        ++cursor_offset;
        continue;
      }

      if (byte == 0x02U || byte == 0x06U) {
        sync_pending_draft(draft,
                           cursor_offset,
                           byte == 0x02U ? "interactive_text_before_ctrl_b"
                                         : "interactive_text_before_ctrl_f");
        static_cast<void>(composer_.handle_key(view::TuiComposerKeyEvent{
            .key = byte == 0x02U ? view::TuiComposerKey::Left
                                 : view::TuiComposerKey::Right,
            .text = {}}));
        sync_composer_state();
        draft = composer_.state().text;
        cursor_offset = composer_.state().cursor_offset;
        continue;
      }

      if (byte == 0x12U) {
        sync_pending_draft(draft, cursor_offset, "interactive_text_before_reverse_search");
        static_cast<void>(composer_.handle_key(
            view::TuiComposerKeyEvent{.key = view::TuiComposerKey::CtrlR, .text = {}}));
        sync_composer_state();
        draft = composer_.state().text;
        cursor_offset = composer_.state().cursor_offset;
        continue;
      }

      if (byte == 0x7FU || byte == 0x08U) {
        erase_previous_text_token(draft, cursor_offset);
        continue;
      }

      insert_printable_text(draft,
                            cursor_offset,
                            std::string_view(input_buffer + offset, 1U));
    }

    sync_pending_draft(draft, cursor_offset, "interactive_text_changed");
    redraw();
  }

  interactive_redraw_hook_ = nullptr;
  return shutdown();
#endif
}

void TuiApp::dispatch_action(const model::TuiAction& action) {
  apply_reduced_action(screen_model_, action);

  switch (action.type) {
    case TuiActionType::StatusUpdated:
    case TuiActionType::EventAppended:
      sync_composer_busy_from_status();
      break;

    case TuiActionType::StatusQueryRequested:
      apply_reduced_action(screen_model_,
                           make_banner_action(model::TuiBannerLevel::Info,
                                              "Status summary",
                                              status_summary_message(screen_model_.status),
                                              "status_query",
                                              "status_query_requested"));
      break;

    case TuiActionType::SessionQueryRequested: {
      TuiModalState modal;
      modal.kind = TuiModalKind::Session;
      modal.title = "Session summary";
      modal.body = session_modal_body(screen_model_.session, screen_model_.route);
      modal.actions = {"Close"};
      modal.selected_action_index = 0;
      apply_reduced_action(screen_model_,
                           make_modal_action(std::move(modal),
                                             "session_query_requested"));
      break;
    }

    case TuiActionType::ForegroundSessionClearRequested:
      handle_foreground_session_clear();
      break;

    case TuiActionType::TurnSubmitRequested:
      dispatch_composer_submit();
      break;

    case TuiActionType::ExitRequested:
      exit_requested_ = true;
      shutdown_close_reason_ = "/exit";
      break;

    case TuiActionType::RouteUpdated:
    case TuiActionType::TranscriptMessageAppended:
    case TuiActionType::Noop:
    case TuiActionType::FocusChanged:
    case TuiActionType::BannerAdded:
    case TuiActionType::BannerCleared:
    case TuiActionType::ModalShown:
    case TuiActionType::ModalHidden:
    case TuiActionType::SessionHydrated:
    case TuiActionType::ComposerTextChanged:
    case TuiActionType::ComposerModeChanged:
    case TuiActionType::ComposerSubmitAvailabilityChanged:
    case TuiActionType::ForegroundSessionResetApplied:
      break;
  }

  render_current_screen(false);
}

bool TuiApp::tick() {
  if (!session_open_ || session_id_.empty() || data_source_ == nullptr) {
    return false;
  }

  const data::TuiPollEventsRequest request{
      .session_id = session_id_,
      .event_cursor = last_event_cursor_,
      .request_id = next_request_id("poll_events"),
      .trace_id = next_trace_id("poll_events"),
  };
  data::TuiPollEventsResult result = data_source_->poll_events(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = result.issue->message;
      append_issue_banner(*result.issue, "Event poll failed");
      render_current_screen(false);
    }
    return false;
  }

  last_event_cursor_ = result.next_cursor;
  if (result.events.empty()) {
    return false;
  }

  for (const auto& event : result.events) {
    TuiAction action;
    action.type = TuiActionType::EventAppended;
    action.debug_reason = "tick:" + event.event_kind;
    action.event = event;
    dispatch_action(action);
  }

  return true;
}

int TuiApp::shutdown() {
  if (!session_open_ || session_id_.empty() || data_source_ == nullptr) {
    shutdown_clean_ = last_error_.empty();
    return last_error_.empty() ? 0 : 1;
  }

  const data::TuiCloseSessionRequest request{
      .session_id = session_id_,
      .close_reason = shutdown_close_reason_,
      .request_id = next_request_id("close_session"),
      .trace_id = next_trace_id("close_session"),
  };
  data::TuiCloseSessionResult result = data_source_->close_session(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = result.issue->message;
      append_issue_banner(*result.issue, "Session close failed");
      render_current_screen(false);
    }
    shutdown_clean_ = false;
    return 1;
  }

  session_open_ = false;
  shutdown_clean_ = true;
  return 0;
}

void TuiApp::clear_composer_draft_preserving_history() {
  static_cast<void>(composer_.set_busy(false));
  const view::TuiComposerKeyEvent event{
      .key = view::TuiComposerKey::TextChanged,
      .text = std::string{},
  };
  static_cast<void>(composer_.handle_key(event));
}

void TuiApp::set_composer_text(std::string text,
                               const std::optional<std::size_t> cursor_offset,
                               std::string debug_reason) {
  const view::TuiComposerKeyEvent event{
      .key = view::TuiComposerKey::TextChanged,
      .text = std::move(text),
      .cursor_offset = cursor_offset,
  };
  static_cast<void>(composer_.handle_key(event));
  sync_composer_state();
  screen_model_.debug_reason = std::move(debug_reason);
}

void TuiApp::sync_pending_draft(const std::string& draft,
                                const std::size_t cursor_offset,
                                std::string debug_reason) {
  const std::size_t clamped_cursor =
      view::clamp_to_terminal_text_offset(draft, cursor_offset);
  if (draft == composer_.state().text &&
      clamped_cursor == composer_.state().cursor_offset) {
    return;
  }

  set_composer_text(draft, clamped_cursor, std::move(debug_reason));
}

void TuiApp::restore_composer_draft(std::string text,
                                    const std::size_t cursor_offset) {
  static_cast<void>(composer_.set_busy(false));
  const view::TuiComposerKeyEvent event{
      .key = view::TuiComposerKey::TextChanged,
      .text = std::move(text),
      .cursor_offset = cursor_offset,
  };
  static_cast<void>(composer_.handle_key(event));
  sync_composer_state();
}

void TuiApp::dispatch_composer_submit() {
  if (!session_open_ || session_id_.empty() || data_source_ == nullptr) {
    apply_reduced_action(screen_model_,
                         make_banner_action(model::TuiBannerLevel::Error,
                                            "Turn submit unavailable",
                                            "No active session is attached to the formal submit path.",
                                            "submit_unavailable",
                                            "turn_submit_unavailable",
                                            true));
    return;
  }

  const std::string previous_draft = composer_.state().text;
  const std::size_t previous_cursor_offset = composer_.state().cursor_offset;
  const view::TuiComposerUpdate update = composer_.handle_key(
      view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Enter, .text = {}});
  sync_composer_state();

  if (update.action.type != view::TuiComposerActionType::SubmitRequested) {
    return;
  }

  set_processing_indicator();
  request_interactive_redraw();

  const data::TuiSubmitTurnRequest request{
      .session_id = session_id_,
      .user_input = update.action.text,
      .next_preference = screen_model_.route.next_preference,
      .request_id = next_request_id("submit_turn"),
      .trace_id = next_trace_id("submit_turn"),
  };
  data::TuiSubmitTurnResult result = data_source_->submit_turn(request);
  if (!result.ok()) {
    restore_composer_draft(previous_draft, previous_cursor_offset);
    if (result.issue.has_value()) {
      last_error_ = result.issue->message;
      append_issue_banner(*result.issue,
                          result.issue->reason_code == "validation_failed"
                              ? "Turn submit rejected"
                              : "Turn submit failed");
    }
    return;
  }
  if (!result.receipt.has_value()) {
    restore_composer_draft(previous_draft, previous_cursor_offset);
    apply_reduced_action(screen_model_,
                         make_banner_action(model::TuiBannerLevel::Error,
                                            "Turn submit failed",
                                            "submit_turn succeeded without a receipt payload.",
                                            "missing_submit_receipt",
                                            "turn_submit_missing_receipt",
                                            true));
    return;
  }

  last_error_.clear();
  apply_reduced_action(screen_model_,
                       make_transcript_message_action(
                           "user",
                           update.action.text,
                           result.receipt->submitted_at,
                           {"submitted"},
                           "submit_turn_user_message"));
  apply_reduced_action(screen_model_,
                       make_banner_action(
                           model::TuiBannerLevel::Info,
                           "Turn submitted",
                           result.receipt->summary_text.empty()
                               ? result.receipt->disposition
                               : result.receipt->summary_text,
                           result.receipt->reason_code.value_or(std::string{}),
                           "turn_submit_requested"));
}

void TuiApp::handle_interactive_submit() {
  const command::TuiSlashCommandParser parser;
  const command::TuiSlashCommandParseResult parse_result =
      parser.parse(composer_.state().text);
  if (!parse_result.is_slash_command) {
    dispatch_composer_submit();
    return;
  }

  if (!parse_result.accepted) {
    dispatch_action(parse_result.to_action());
    return;
  }

  clear_composer_draft_preserving_history();
  sync_composer_state();

  if (parse_result.command.kind == command::TuiSlashCommandKind::Editor) {
    apply_reduced_action(screen_model_,
                         make_banner_action(model::TuiBannerLevel::Warning,
                                            "External editor unavailable",
                                            "Formal TUI interactive editor wiring is not available yet.",
                                            "external_editor_unavailable",
                                            "interactive_editor_unavailable",
                                            true));
    return;
  }

  dispatch_action(parse_result.to_action());
}

void TuiApp::handle_foreground_session_clear() {
  std::optional<data::TuiDataSourceIssue> close_issue;
  if (session_open_ && !session_id_.empty() && data_source_ != nullptr) {
    const data::TuiCloseSessionRequest close_request{
        .session_id = session_id_,
        .close_reason = "/clear",
        .request_id = next_request_id("close_session"),
        .trace_id = next_trace_id("close_session"),
    };
    const data::TuiCloseSessionResult close_result =
        data_source_->close_session(close_request);
    if (!close_result.ok() && close_result.issue.has_value()) {
      close_issue = close_result.issue;
    }
  }

  session_open_ = false;
  session_id_.clear();
  last_event_cursor_.reset();
  last_error_.clear();

  clear_composer_draft_preserving_history();
  apply_reduced_action(screen_model_,
                       make_foreground_session_reset_action(
                           "foreground_session_clear_reset"));
  sync_composer_state();

  const bool session_reopened = open_session();
  const bool route_reloaded = session_reopened && load_route_catalog();

  if (close_issue.has_value()) {
    apply_reduced_action(screen_model_,
                         make_banner_action(model::TuiBannerLevel::Warning,
                                            "Previous session close not confirmed",
                                            close_issue->message,
                                            close_issue->reason_code,
                                            "foreground_session_clear_close_issue",
                                            true));
  }

  if (session_reopened && route_reloaded) {
    sync_composer_busy_from_status();
  }
}

void TuiApp::hide_modal(std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::ModalHidden;
  action.debug_reason = std::move(debug_reason);
  dispatch_action(action);
}

const model::TuiScreenModel& TuiApp::screen_model() const noexcept {
  return screen_model_;
}

const std::vector<std::string>& TuiApp::rendered_frames() const noexcept {
  return rendered_frames_;
}

const std::string& TuiApp::last_rendered_screen() const noexcept {
  return last_rendered_screen_;
}

const terminal::TuiTerminalCapabilities& TuiApp::terminal_capabilities() const noexcept {
  return terminal_capabilities_;
}

terminal::TuiStartupMode TuiApp::startup_mode() const noexcept {
  return startup_mode_;
}

bool TuiApp::session_open() const noexcept {
  return session_open_;
}

bool TuiApp::shutdown_clean() const noexcept {
  return shutdown_clean_;
}

std::string_view TuiApp::last_error() const noexcept {
  return last_error_;
}

void TuiApp::initialize_components(TuiAppOptions& options) {
  const bool has_initial_draft = options.initial_draft.has_value() &&
                                 !options.initial_draft->empty();

  output_stream_ = options.output_stream;
  scenario_id_ = options.scenario_id.empty() ? "planning_tools" : options.scenario_id;
  profile_id_ = options.profile_id;
  composer_ = view::TuiComposer(model::TuiComposerState{
      .text = options.initial_draft.value_or(std::string{}),
      .mode = has_initial_draft ? "editing" : "ready",
      .history_query = std::nullopt,
      .can_submit = true,
      .dirty = has_initial_draft,
        .cursor_visible = true,
        .activity_indicator = {},
  });
  selector_ = view::TuiModelSelector{};
  screen_model_ = model::TuiScreenModel{};
  screen_model_.composer = composer_.state();
  screen_model_.focus = model::TuiFocusState::Composer;
  screen_model_.debug_reason = "tui_app_initialized";
  terminal_capabilities_ = {};
  startup_mode_ = terminal::TuiStartupMode::FailClosed;
  last_event_cursor_.reset();
  rendered_frames_.clear();
  last_rendered_screen_.clear();
  last_error_.clear();
  data_source_ = std::move(options.data_source_override);
  if (data_source_ == nullptr) {
    last_error_ = "no TUI data source configured for this entrypoint";
  }
  session_id_.clear();
  request_sequence_ = 0;
  session_open_ = false;
  shutdown_clean_ = false;
  shutdown_close_reason_ = "prototype_round_complete";
  exit_requested_ = false;
}

bool TuiApp::open_session() {
  const data::TuiOpenSessionRequest request{
      .profile_id = profile_id_,
      .startup_mode_hint = startup_mode_to_string(startup_mode_),
      .request_id = next_request_id("open_session"),
      .trace_id = next_trace_id("open_session"),
  };
  data::TuiOpenSessionResult result = data_source_->open_session(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = format_startup_issue_message(*result.issue);
      append_issue_banner(*result.issue, "Session open failed");
    }
    return false;
  }

  session_id_ = result.session->session_id;
  session_open_ = true;

  TuiAction session_action;
  session_action.type = TuiActionType::SessionHydrated;
  session_action.debug_reason = "open_session";
  session_action.session = result.session;
  apply_reduced_action(screen_model_, session_action);

  TuiAction status_action;
  status_action.type = TuiActionType::StatusUpdated;
  status_action.debug_reason = "baseline_status";
  status_action.status = make_baseline_status(startup_mode_to_string(startup_mode_));
  apply_reduced_action(screen_model_, status_action);
  sync_composer_busy_from_status();
  return true;
}

bool TuiApp::load_route_catalog() {
  const data::TuiRouteCatalogRequest request{
      .session_id = session_id_.empty() ? std::nullopt : std::optional<std::string>{session_id_},
      .profile_id = profile_id_,
      .selector_mode = std::nullopt,
      .request_id = next_request_id("route_catalog"),
      .trace_id = next_trace_id("route_catalog"),
  };
  data::TuiRouteCatalogResult result = data_source_->route_catalog(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = format_startup_issue_message(*result.issue);
      append_issue_banner(*result.issue, "Route catalog failed");
    }
    return false;
  }

  selector_.set_route_catalog(*result.route_catalog);
  apply_reduced_action(screen_model_,
                       make_route_action(result.route_catalog->current_route,
                                         "route_catalog_loaded"));
  return true;
}

void TuiApp::sync_composer_state() {
  TuiAction text_action;
  text_action.type = TuiActionType::ComposerTextChanged;
  text_action.debug_reason = "composer_state_sync:text";
  text_action.composer_text = composer_.state().text;
  apply_reduced_action(screen_model_, text_action);

  TuiAction mode_action;
  mode_action.type = TuiActionType::ComposerModeChanged;
  mode_action.debug_reason = "composer_state_sync:mode";
  mode_action.composer_mode = composer_.state().mode;
  apply_reduced_action(screen_model_, mode_action);

  TuiAction submit_action;
  submit_action.type = TuiActionType::ComposerSubmitAvailabilityChanged;
  submit_action.debug_reason = "composer_state_sync:submit";
  submit_action.composer_can_submit = composer_.state().can_submit;
  apply_reduced_action(screen_model_, submit_action);

  screen_model_.composer.cursor_offset = composer_.state().cursor_offset;
  screen_model_.composer.history_query = composer_.state().history_query;
  screen_model_.composer.dirty = composer_.state().dirty;
  screen_model_.composer.cursor_visible = composer_.state().cursor_visible;
  screen_model_.composer.activity_indicator = composer_.state().activity_indicator;
}

void TuiApp::sync_composer_busy_from_status() {
  const bool busy = status_requires_busy_composer(screen_model_.status);
  static_cast<void>(composer_.set_busy(busy));
  sync_composer_state();
  if (busy) {
    set_processing_indicator();
  } else {
    screen_model_.composer.activity_indicator.clear();
  }
}

void TuiApp::set_processing_indicator() {
  if (animation_spinner_index_ == 0U) {
    screen_model_.composer.activity_indicator = dots_spinner_frame(0U);
    return;
  }

  screen_model_.composer.activity_indicator = dots_spinner_frame(animation_spinner_index_);
}

void TuiApp::request_interactive_redraw() {
  if (interactive_redraw_hook_) {
    interactive_redraw_hook_();
    return;
  }

  render_current_screen(false);
}

void TuiApp::scroll_transcript_page(const int direction) {
  const view::TuiLayoutMetrics metrics = renderer_.apply_layout_metrics(
      effective_terminal_width(),
      effective_terminal_height());
  const std::size_t transcript_width = saturating_sub_size(metrics.transcript.width, 2U);
  const std::size_t transcript_height = saturating_sub_size(metrics.transcript.height, 2U);
  const std::size_t transcript_wrap_width =
      transcript_width > renderer_.design_tokens().spacing.transcript_indent
          ? transcript_width - renderer_.design_tokens().spacing.transcript_indent
          : 1U;

  view::TuiTranscriptView transcript_view(screen_model_.transcript);
  if (screen_model_.transcript_follow_tail) {
    transcript_view.scroll_to_bottom(transcript_height, transcript_wrap_width);
  } else {
    transcript_view.set_scroll_offset(screen_model_.transcript_scroll_offset);
  }
  const auto rendered_transcript = transcript_view.render_transcript(
      transcript_height,
      transcript_wrap_width);
  const std::size_t max_offset = transcript_max_scroll_offset(
      rendered_transcript.total_line_count,
      transcript_height);
  if (max_offset == 0U) {
    screen_model_.transcript_scroll_offset = 0;
    screen_model_.transcript_follow_tail = true;
    return;
  }

  const std::size_t current_offset = rendered_transcript.scroll_offset;
  const std::size_t page_step = transcript_height > 1U ? transcript_height - 1U : 1U;
  if (direction < 0) {
    screen_model_.transcript_scroll_offset = current_offset > page_step
        ? current_offset - page_step
        : 0U;
    screen_model_.transcript_follow_tail = false;
  } else {
    screen_model_.transcript_scroll_offset = std::min(max_offset,
                                                      current_offset + page_step);
    screen_model_.transcript_follow_tail =
        screen_model_.transcript_scroll_offset >= max_offset;
  }
  screen_model_.focus = model::TuiFocusState::Transcript;
  screen_model_.debug_reason = direction < 0 ? "transcript_page_up" : "transcript_page_down";
}

bool TuiApp::advance_interactive_animation() {
  bool changed = false;

  ++animation_cursor_tick_;
  if (animation_cursor_tick_ >= 5U) {
    animation_cursor_tick_ = 0;
    animation_cursor_visible_ = !animation_cursor_visible_;
    changed = true;
  }

  if (screen_model_.composer.cursor_visible != animation_cursor_visible_) {
    screen_model_.composer.cursor_visible = animation_cursor_visible_;
    changed = true;
  }

  if (composer_waiting_for_response(screen_model_.composer)) {
    animation_spinner_index_ = (animation_spinner_index_ + 1U) % 3U;
    const std::string indicator = dots_spinner_frame(animation_spinner_index_);
    if (screen_model_.composer.activity_indicator != indicator) {
      screen_model_.composer.activity_indicator = indicator;
      changed = true;
    }
    return changed;
  }

  animation_spinner_index_ = 0;
  if (!screen_model_.composer.activity_indicator.empty()) {
    screen_model_.composer.activity_indicator.clear();
    changed = true;
  }
  return changed;
}

void TuiApp::show_selector_preview(const data::TuiRoutePreferenceMode mode) {
  const auto options = selector_.open_selector(mode);
  const data::NextTurnPreference next_preference = selector_.apply_preference();

  data::TuiModelRouteProjection route = screen_model_.route;
  route.next_preference = next_preference;
  apply_reduced_action(screen_model_,
                       make_route_action(std::move(route),
                                         "selector_preview_applied"));

  TuiModalState modal;
  modal.kind = TuiModalKind::Selector;
  modal.title = "Next turn preference";
  modal.body = selector_modal_body(options);
  modal.actions = {"Apply", "Cancel"};
  modal.selected_action_index = 0;
  apply_reduced_action(screen_model_,
                       make_modal_action(std::move(modal),
                                         "selector_preview_modal"));
}

void TuiApp::render_current_screen(const bool flush_to_output) {
  last_rendered_screen_ = renderer_.render_to_screen(
      screen_model_,
      effective_terminal_width(),
      effective_terminal_height());
  rendered_frames_.push_back(last_rendered_screen_);

  if (flush_to_output && output_stream_ != nullptr) {
    *output_stream_ << last_rendered_screen_ << '\n';
  }
}

void TuiApp::emit_startup_error() const {
  if (output_stream_ != nullptr && !last_error_.empty()) {
    *output_stream_ << last_error_ << '\n';
  }
}

std::string TuiApp::selector_modal_body(
    const std::vector<view::TuiModelSelectorOption>& options) const {
  if (options.empty()) {
    return "No selector options are required for auto mode.";
  }

  std::ostringstream body;
  body << "Draft: " << screen_model_.route.next_preference.user_visible_summary << '\n';
  for (const auto& option : options) {
    body << (option.selected ? "* " : "- ") << option.display_label;
    if (!option.selectable) {
      body << " [disabled: "
           << selector_.render_disabled_reason(option.disabled_reasons)
           << ']';
    }
    body << '\n';
  }
  return body.str();
}

std::size_t TuiApp::effective_terminal_width() const noexcept {
  if (terminal_width_ > 0) {
    return terminal_width_;
  }
  if (terminal_capabilities_.columns > 0) {
    return static_cast<std::size_t>(terminal_capabilities_.columns);
  }

  switch (startup_mode_) {
    case terminal::TuiStartupMode::FullScreen:
      return 120;
    case terminal::TuiStartupMode::Narrow:
      return 80;
    case terminal::TuiStartupMode::Line:
    case terminal::TuiStartupMode::FailClosed:
      return 40;
  }

  return 40;
}

std::size_t TuiApp::effective_terminal_height() const noexcept {
  if (terminal_height_ > 0) {
    return terminal_height_;
  }
  if (terminal_capabilities_.rows > 0) {
    return static_cast<std::size_t>(terminal_capabilities_.rows);
  }

  switch (startup_mode_) {
    case terminal::TuiStartupMode::FullScreen:
      return 36;
    case terminal::TuiStartupMode::Narrow:
      return 24;
    case terminal::TuiStartupMode::Line:
    case terminal::TuiStartupMode::FailClosed:
      return 12;
  }

  return 12;
}

std::string TuiApp::format_startup_issue_message(
    const data::TuiDataSourceIssue& issue) {
  const std::string detail = trim_copy(issue.message);

  if (issue.reason_code == "permission_denied") {
    std::string message =
        "TUI startup blocked: permission denied for the current root/sudo-only operator path.";
    if (!detail.empty()) {
      message += ' ';
      message += detail;
    }
    return message;
  }

  if (startup_issue_means_daemon_unavailable(issue)) {
    std::string message =
        "TUI startup blocked: daemon unavailable for the local control-plane path.";
    if (!detail.empty()) {
      message += ' ';
      message += detail;
    }
    return message;
  }

  if (issue.reason_code == "profile_missing") {
    std::string message =
        "TUI startup blocked: requested profile is missing or incomplete.";
    if (!detail.empty()) {
      message += ' ';
      message += detail;
    }
    return message;
  }

  if (!detail.empty()) {
    return "TUI startup blocked: " + detail;
  }

  if (issue.has_reason()) {
    return "TUI startup blocked: " + issue.reason_domain + '/' + issue.reason_code;
  }

  return "TUI startup blocked: startup requirements are not satisfied.";
}

std::string TuiApp::startup_mode_to_string(
    const terminal::TuiStartupMode startup_mode) {
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

bool TuiApp::status_requires_busy_composer(const data::TuiStatusProjection& status) {
  const std::string stage = trim_copy(status.stage);
  if (stage == "completed" || stage == "rejected" || stage == "cancelled" ||
      stage == "timeout" || stage == "failed") {
    return false;
  }

  const std::string pending_interaction = trim_copy(status.pending_interaction);
  if (!pending_interaction.empty() && pending_interaction != "none") {
    return true;
  }

  return stage == "accepted_async" || stage == "tool_calling" ||
         stage == "waiting_interaction";
}

std::string TuiApp::next_request_id(std::string_view prefix) {
  ++request_sequence_;
  return std::string(prefix) + "-" + scenario_id_ + "-" +
         std::to_string(request_sequence_);
}

std::string TuiApp::next_trace_id(std::string_view prefix) {
  return "trace-" + next_request_id(prefix);
}

void TuiApp::append_issue_banner(const data::TuiDataSourceIssue& issue, std::string title) {
  apply_reduced_action(screen_model_,
                       make_banner_action(model::TuiBannerLevel::Error,
                                          std::move(title),
                                          issue.message,
                                          issue.reason_code,
                                          "data_source_issue:" + issue.reason_code,
                                          true));
}

}  // namespace dasall::tui::app