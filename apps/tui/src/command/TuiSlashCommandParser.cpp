#include "command/TuiSlashCommandParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::tui::command {
namespace {

using dasall::tui::model::TuiAction;
using dasall::tui::model::TuiActionType;
using dasall::tui::model::TuiBanner;
using dasall::tui::model::TuiBannerLevel;
using dasall::tui::model::TuiModalKind;
using dasall::tui::model::TuiModalState;

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();

  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::string to_lower_ascii(std::string value) {
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

[[nodiscard]] std::vector<std::string> split_tokens(std::string_view text) {
  std::vector<std::string> tokens;
  std::size_t cursor = 0;

  while (cursor < text.size()) {
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
      ++cursor;
    }

    const std::size_t token_begin = cursor;
    while (cursor < text.size() &&
           std::isspace(static_cast<unsigned char>(text[cursor])) == 0) {
      ++cursor;
    }

    if (token_begin < cursor) {
      tokens.emplace_back(text.substr(token_begin, cursor - token_begin));
    }
  }

  return tokens;
}

[[nodiscard]] bool contains_line_break(std::string_view text) {
  return text.find('\n') != std::string_view::npos ||
         text.find('\r') != std::string_view::npos;
}

[[nodiscard]] std::vector<TuiSlashCommandHelpEntry> build_help_entries() {
  return {
      TuiSlashCommandHelpEntry{"/help", "Show TUI help and keymap.", "TUI local"},
      TuiSlashCommandHelpEntry{"/status",
                               "Query session, daemon, and runtime status summary.",
                               "daemon/access projection"},
      TuiSlashCommandHelpEntry{"/session",
                               "Show current session id, profile, and route summary.",
                               "daemon/access projection"},
      TuiSlashCommandHelpEntry{"/clear",
                               "Reset the foreground session view and keep input history.",
                               "TUI + session seam"},
      TuiSlashCommandHelpEntry{"/editor",
                               "Open the external editor for long-form input.",
                               "TUI local"},
      TuiSlashCommandHelpEntry{"/exit",
                               "Close the foreground session and exit.",
                               "TUI + daemon session close"},
  };
}

[[nodiscard]] std::string build_help_body() {
  std::ostringstream body;
  body << "Supported slash commands:\n";

  for (const auto& entry : build_help_entries()) {
    body << "- " << entry.command << ": " << entry.summary << " ["
         << entry.owner_boundary << "]\n";
  }

  body << "\nKeymap: Enter send | Alt+Enter newline | Ctrl+R history | /editor external editor";
  return body.str();
}

[[nodiscard]] TuiAction build_error_banner_action(const TuiSlashCommandParseResult& result) {
  TuiAction action;
  action.type = TuiActionType::BannerAdded;
  action.debug_reason = result.normalized_input.empty()
                            ? "slash_command_error"
                            : "slash_command_error:" + result.normalized_input;

  TuiBanner banner;
  banner.level = TuiBannerLevel::Error;
  banner.title = result.command.kind == TuiSlashCommandKind::Unknown
                     ? "Unknown slash command"
                     : "Invalid slash command";
  banner.message = result.error_message;
  if (!result.suggestion.empty()) {
    banner.message += " " + result.suggestion;
  }
  if (!result.reason_code.empty()) {
    banner.reason_code = result.reason_code;
  }

  action.banner = std::move(banner);
  return action;
}

[[nodiscard]] TuiSlashCommandParseResult make_rejected_result(std::string normalized_input,
                                                              std::string reason_code,
                                                              std::string error_message,
                                                              std::string suggestion,
                                                              TuiSlashCommand command) {
  TuiSlashCommandParseResult result;
  result.command = std::move(command);
  result.is_slash_command = true;
  result.accepted = false;
  result.normalized_input = std::move(normalized_input);
  result.reason_code = std::move(reason_code);
  result.error_message = std::move(error_message);
  result.suggestion = std::move(suggestion);
  return result;
}

[[nodiscard]] TuiSlashCommandParseResult make_accepted_result(std::string normalized_input,
                                                              TuiSlashCommand command) {
  TuiSlashCommandParseResult result;
  result.command = std::move(command);
  result.is_slash_command = true;
  result.accepted = true;
  result.normalized_input = std::move(normalized_input);
  return result;
}

}  // namespace

TuiAction TuiSlashCommandParseResult::to_action() const {
  if (!is_slash_command) {
    TuiAction action;
    action.type = TuiActionType::Noop;
    action.debug_reason = normalized_input.empty() ? "not_slash_command" : normalized_input;
    return action;
  }

  if (!accepted) {
    return build_error_banner_action(*this);
  }

  TuiAction action;
  action.debug_reason = "slash_command:" + normalized_input;

  switch (command.kind) {
    case TuiSlashCommandKind::Help: {
      action.type = TuiActionType::ModalShown;

      TuiModalState modal;
      modal.kind = TuiModalKind::Help;
      modal.title = "TUI Slash Commands";
      modal.body = build_help_body();
      modal.actions = {"Close"};
      modal.selected_action_index = 0;
      action.modal = std::move(modal);
      return action;
    }

    case TuiSlashCommandKind::Status:
      action.type = TuiActionType::StatusQueryRequested;
      return action;

    case TuiSlashCommandKind::Session:
      action.type = TuiActionType::SessionQueryRequested;
      return action;

    case TuiSlashCommandKind::Clear:
      action.type = TuiActionType::ForegroundSessionClearRequested;
      return action;

    case TuiSlashCommandKind::Editor:
      action.type = TuiActionType::ComposerModeChanged;
      action.composer_mode = std::string{"external-editor"};
      return action;

    case TuiSlashCommandKind::Exit:
      action.type = TuiActionType::ExitRequested;
      return action;

    case TuiSlashCommandKind::None:
    case TuiSlashCommandKind::Unknown:
      return build_error_banner_action(*this);
  }

  return build_error_banner_action(*this);
}

TuiSlashCommandParseResult TuiSlashCommandParser::parse(std::string_view line) const {
  if (contains_line_break(line)) {
    return {};
  }

  const std::string trimmed = trim_copy(line);
  if (trimmed.empty() || trimmed.front() != '/') {
    return {};
  }

  const std::vector<std::string> tokens = split_tokens(trimmed);
  if (tokens.empty()) {
    return {};
  }

  const std::string normalized_input = to_lower_ascii(tokens.front());
  if (normalized_input == "/") {
    TuiSlashCommand command;
    command.kind = TuiSlashCommandKind::Unknown;
    command.verb = normalized_input;
    command.local_action = true;

    return make_rejected_result(normalized_input,
                                "empty_slash_command",
                                "Slash command cannot be empty.",
                                "Try /help for the supported command set.",
                                std::move(command));
  }

  if (tokens.size() > 1) {
    TuiSlashCommand command;
    command.kind = TuiSlashCommandKind::Unknown;
    command.verb = normalized_input;
    command.local_action = true;

    return make_rejected_result(normalized_input,
                                "slash_command_arguments_not_supported",
                                "Slash commands do not accept arguments in TUI v1.",
                                "Remove extra arguments or use /help.",
                                std::move(command));
  }

  TuiSlashCommand command;
  command.verb = normalized_input;

  if (normalized_input == "/help") {
    command.kind = TuiSlashCommandKind::Help;
    command.local_action = true;
    return make_accepted_result(normalized_input, std::move(command));
  }
  if (normalized_input == "/status") {
    command.kind = TuiSlashCommandKind::Status;
    command.projection_query = true;
    return make_accepted_result(normalized_input, std::move(command));
  }
  if (normalized_input == "/session") {
    command.kind = TuiSlashCommandKind::Session;
    command.projection_query = true;
    return make_accepted_result(normalized_input, std::move(command));
  }
  if (normalized_input == "/clear") {
    command.kind = TuiSlashCommandKind::Clear;
    command.local_action = true;
    return make_accepted_result(normalized_input, std::move(command));
  }
  if (normalized_input == "/editor") {
    command.kind = TuiSlashCommandKind::Editor;
    command.local_action = true;
    return make_accepted_result(normalized_input, std::move(command));
  }
  if (normalized_input == "/exit") {
    command.kind = TuiSlashCommandKind::Exit;
    command.local_action = true;
    return make_accepted_result(normalized_input, std::move(command));
  }

  command.kind = TuiSlashCommandKind::Unknown;
  command.local_action = true;
  return make_rejected_result(normalized_input,
                              "unknown_slash_command",
                              "Unknown slash command '" + normalized_input + "'.",
                              "Try /help for the supported command set.",
                              std::move(command));
}

std::vector<TuiSlashCommandHelpEntry> TuiSlashCommandParser::help_entries() {
  return build_help_entries();
}

}  // namespace dasall::tui::command