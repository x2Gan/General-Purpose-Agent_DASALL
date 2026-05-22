#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "command/TuiSlashCommandParser.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_SLASH_COMMAND_HEADER
#define DASALL_TUI_SLASH_COMMAND_HEADER "/home/gangan/DASALL/apps/tui/src/command/TuiSlashCommandParser.h"
#endif

#ifndef DASALL_TUI_SLASH_COMMAND_IMPL
#define DASALL_TUI_SLASH_COMMAND_IMPL "/home/gangan/DASALL/apps/tui/src/command/TuiSlashCommandParser.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::command::TuiSlashCommandKind;
using dasall::tui::command::TuiSlashCommandParser;
using dasall::tui::model::TuiActionType;
using dasall::tui::model::TuiModalKind;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void parser_maps_supported_commands_to_local_or_projection_actions() {
  const TuiSlashCommandParser parser;

  struct ExpectedCase {
    std::string input;
    TuiSlashCommandKind kind;
    bool local_action;
    bool projection_query;
    TuiActionType action_type;
  };

  const std::vector<ExpectedCase> cases = {
      {"/help", TuiSlashCommandKind::Help, true, false, TuiActionType::ModalShown},
      {" /status ", TuiSlashCommandKind::Status, false, true, TuiActionType::StatusQueryRequested},
      {"/session", TuiSlashCommandKind::Session, false, true, TuiActionType::SessionQueryRequested},
      {"/clear", TuiSlashCommandKind::Clear, true, false, TuiActionType::ForegroundSessionClearRequested},
      {"/EDITOR", TuiSlashCommandKind::Editor, true, false, TuiActionType::ComposerModeChanged},
      {"/exit", TuiSlashCommandKind::Exit, true, false, TuiActionType::ExitRequested},
  };

  for (const auto& expected_case : cases) {
    const auto result = parser.parse(expected_case.input);
    const auto action = result.to_action();

    assert_true(result.is_slash_command,
                "supported commands should be recognized as slash commands");
    assert_true(result.accepted,
                "supported commands should parse successfully");
    assert_equal(static_cast<int>(expected_case.kind),
                 static_cast<int>(result.command.kind),
                 "supported commands should map to the expected command kind");
    assert_equal(expected_case.local_action,
                 result.command.local_action,
                 "supported commands should preserve local-action ownership");
    assert_equal(expected_case.projection_query,
                 result.command.projection_query,
                 "supported commands should preserve projection-query ownership");
    assert_equal(static_cast<int>(expected_case.action_type),
                 static_cast<int>(action.type),
                 "supported commands should map to the expected action type");
  }

  const auto help_result = parser.parse("/help");
  const auto help_action = help_result.to_action();
  assert_true(help_action.modal.has_value(),
              "/help should project into a help modal");
  assert_equal(static_cast<int>(TuiModalKind::Help),
               static_cast<int>(help_action.modal->kind),
               "/help should show the help modal kind");
  assert_true(help_action.modal->body.find("/clear") != std::string::npos,
              "help modal should document /clear semantics");

  const auto editor_result = parser.parse("/editor");
  const auto editor_action = editor_result.to_action();
  assert_true(editor_action.composer_mode.has_value(),
              "/editor should switch composer into external-editor mode");
  assert_equal(std::string{"external-editor"},
               *editor_action.composer_mode,
               "/editor should use the frozen external-editor mode tag");
}

void parser_rejects_unknown_or_invalid_commands_fail_closed() {
  const TuiSlashCommandParser parser;

  const auto unknown_result = parser.parse("/danger");
  const auto unknown_action = unknown_result.to_action();
  assert_true(unknown_result.is_slash_command,
              "unknown command should still be classified as slash input");
  assert_true(!unknown_result.accepted,
              "unknown command should fail closed");
  assert_equal(static_cast<int>(TuiSlashCommandKind::Unknown),
               static_cast<int>(unknown_result.command.kind),
               "unknown command should retain the unknown kind");
  assert_equal(static_cast<int>(TuiActionType::BannerAdded),
               static_cast<int>(unknown_action.type),
               "unknown command should emit a local error banner");
  assert_true(unknown_action.banner.has_value(),
              "unknown command should provide banner payload");
  assert_true(unknown_action.banner->reason_code.has_value(),
              "unknown command should expose stable reason codes");
  assert_equal(std::string{"unknown_slash_command"},
               *unknown_action.banner->reason_code,
               "unknown command should keep the frozen unknown reason code");
  assert_true(unknown_action.banner->message.find("/danger") != std::string::npos,
              "unknown command banner should mention the rejected command");
  assert_true(unknown_action.banner->message.find("/help") != std::string::npos,
              "unknown command banner should suggest /help");

  const auto argument_result = parser.parse("/status now");
  const auto argument_action = argument_result.to_action();
  assert_true(!argument_result.accepted,
              "argument-bearing commands should fail closed in v1");
  assert_equal(static_cast<int>(TuiActionType::BannerAdded),
               static_cast<int>(argument_action.type),
               "argument-bearing commands should stay local and emit a banner");
  assert_true(argument_action.banner.has_value(),
              "argument-bearing commands should return banner payload");
  assert_true(argument_action.banner->reason_code.has_value(),
              "argument-bearing commands should preserve a stable reason code");
  assert_equal(std::string{"slash_command_arguments_not_supported"},
               *argument_action.banner->reason_code,
               "argument-bearing commands should expose the frozen argument reason code");
}

void parser_ignores_non_slash_and_multiline_input() {
  const TuiSlashCommandParser parser;

  const auto plain_result = parser.parse("hello tui");
  const auto plain_action = plain_result.to_action();
  assert_true(!plain_result.is_slash_command,
              "plain composer input should not be treated as a slash command");
  assert_equal(static_cast<int>(TuiActionType::Noop),
               static_cast<int>(plain_action.type),
               "plain composer input should remain a no-op for the slash parser");

  const auto multiline_result = parser.parse("/help\n/details");
  const auto multiline_action = multiline_result.to_action();
  assert_true(!multiline_result.is_slash_command,
              "multiline input should not be parsed as a slash command");
  assert_equal(static_cast<int>(TuiActionType::Noop),
               static_cast<int>(multiline_action.type),
               "multiline input should not synthesize a slash command action");
}

void parser_help_entries_document_owner_boundaries() {
  const std::vector<dasall::tui::command::TuiSlashCommandHelpEntry> entries =
      TuiSlashCommandParser::help_entries();

  assert_equal(6,
               static_cast<int>(entries.size()),
               "help entries should cover the frozen minimal slash command set");

  bool saw_status = false;
  bool saw_clear = false;
  for (const auto& entry : entries) {
    if (entry.command == "/status") {
      saw_status = true;
      assert_equal(std::string{"daemon/access projection"},
                   entry.owner_boundary,
                   "/status should preserve the projection owner boundary");
    }

    if (entry.command == "/clear") {
      saw_clear = true;
      assert_true(entry.summary.find("input history") != std::string::npos,
                  "/clear help entry should explain that input history is retained");
    }
  }

  assert_true(saw_status,
              "help entries should include /status");
  assert_true(saw_clear,
              "help entries should include /clear");
}

void parser_files_avoid_owner_private_includes_and_renderer_dependencies() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_SLASH_COMMAND_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_SLASH_COMMAND_IMPL});

  for (const std::string* file_text : {&header_text, &impl_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "slash parser files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "slash parser files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "slash parser files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "slash parser files should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "slash parser files should not leak FTXUI into the parser layer");
    assert_true(file_text->find("iostream") == std::string::npos,
                "slash parser files should not depend on stream I/O in production code");
    assert_true(file_text->find("fstream") == std::string::npos,
                "slash parser files should not depend on file I/O in production code");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "slash parser files should not depend on filesystem APIs in production code");
    assert_true(file_text->find("AgentRequest") == std::string::npos,
                "slash parser files should not bind to access request owners");
    assert_true(file_text->find("RuntimeDispatchRequest") == std::string::npos,
                "slash parser files should not bind to runtime dispatch owners");
  }
}

}  // namespace

int main() {
  try {
    parser_maps_supported_commands_to_local_or_projection_actions();
    parser_rejects_unknown_or_invalid_commands_fail_closed();
    parser_ignores_non_slash_and_multiline_input();
    parser_help_entries_document_owner_boundaries();
    parser_files_avoid_owner_private_includes_and_renderer_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiSlashCommandParserTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}