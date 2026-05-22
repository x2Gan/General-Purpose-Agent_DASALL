#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "model/TuiAction.h"

namespace dasall::tui::command {

enum class TuiSlashCommandKind {
  None,
  Help,
  Status,
  Session,
  Clear,
  Editor,
  Exit,
  Unknown,
};

struct TuiSlashCommand {
  TuiSlashCommandKind kind = TuiSlashCommandKind::None;
  std::string verb;
  bool local_action = false;
  bool projection_query = false;
};

struct TuiSlashCommandHelpEntry {
  std::string command;
  std::string summary;
  std::string owner_boundary;
};

struct TuiSlashCommandParseResult {
  TuiSlashCommand command;
  bool is_slash_command = false;
  bool accepted = false;
  std::string normalized_input;
  std::string reason_code;
  std::string error_message;
  std::string suggestion;

  [[nodiscard]] model::TuiAction to_action() const;
};

class TuiSlashCommandParser {
 public:
  [[nodiscard]] TuiSlashCommandParseResult parse(std::string_view line) const;

  [[nodiscard]] static std::vector<TuiSlashCommandHelpEntry> help_entries();
};

}  // namespace dasall::tui::command