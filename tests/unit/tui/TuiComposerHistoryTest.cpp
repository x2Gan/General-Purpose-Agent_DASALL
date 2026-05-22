#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "support/TestAssertions.h"
#include "view/TuiComposer.h"
#include "view/TuiInputHistory.h"

#ifndef DASALL_TUI_INPUT_HISTORY_HEADER
#define DASALL_TUI_INPUT_HISTORY_HEADER "/home/gangan/DASALL/apps/tui/src/view/TuiInputHistory.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::view::TuiComposer;
using dasall::tui::view::TuiComposerActionType;
using dasall::tui::view::TuiComposerKey;
using dasall::tui::view::TuiComposerKeyEvent;
using dasall::tui::view::TuiInputHistory;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void submit_prompt(TuiComposer& composer, const std::string& prompt) {
    static_cast<void>(
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged, .text = prompt}));
    const auto submitted =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Enter, .text = ""});
  assert_equal(static_cast<int>(TuiComposerActionType::SubmitRequested),
               static_cast<int>(submitted.action.type),
               "history fixture setup should submit a real prompt entry");
  static_cast<void>(composer.set_busy(false));
}

void input_history_tracks_non_blank_entries_and_reverse_matches() {
  TuiInputHistory history;
  history.record("alpha prompt");
  history.record("   ");
  history.record("beta build status");
  history.record("gamma build docs");

  assert_equal(3,
               static_cast<int>(history.size()),
               "input history should ignore blank draft submissions");
  assert_true(!history.empty(),
              "input history should report non-empty after recording prompts");
  assert_equal("gamma build docs",
               history.at(*history.older(std::nullopt)),
               "older(nullopt) should start recall from the newest prompt");
  assert_equal("gamma build docs",
               history.at(*history.latest_match("build")),
               "latest_match should return the newest history item containing the query");
  assert_equal("beta build status",
               history.at(*history.latest_match("build", history.latest_match("build"))),
               "latest_match(query, before) should continue reverse search to older prompts");
  assert_true(!history.latest_match("missing").has_value(),
              "latest_match should return nullopt when no prompt matches the query");
}

void composer_recalls_history_only_at_boundary_and_restores_seed_draft() {
  TuiComposer composer;
  submit_prompt(composer, "alpha prompt");
  submit_prompt(composer, "beta prompt");
  submit_prompt(composer, "gamma prompt");

  static_cast<void>(composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged,
                                                            .text = "working draft"}));
  const auto recalled = composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Up,
                                                                .text = "",
                                                                .cursor_at_boundary = true,
                                                                .draft_unmodified = true});
  assert_equal("history-recall",
               recalled.state.mode,
               "boundary Up should enter history recall mode");
  assert_equal("gamma prompt",
               recalled.state.text,
               "first Up recall should pull the newest submitted prompt");
  assert_true(!recalled.state.dirty,
              "history recall should expose committed history without a dirty draft flag");

  const auto older = composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Up,
                                                             .text = "",
                                                             .cursor_at_boundary = true,
                                                             .draft_unmodified = true});
  assert_equal("beta prompt",
               older.state.text,
               "repeated Up recall should walk toward older prompts");

  const auto newer =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Down, .text = ""});
  assert_equal("gamma prompt",
               newer.state.text,
               "Down recall should walk back toward newer prompts");

  const auto restored =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Down, .text = ""});
  assert_equal("editing",
               restored.state.mode,
               "leaving history recall should restore normal editing mode");
  assert_equal("working draft",
               restored.state.text,
               "leaving history recall should restore the seed draft");

  static_cast<void>(composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged,
                                                            .text = "not at boundary"}));
  const auto blocked = composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Up,
                                                               .text = "",
                                                               .cursor_at_boundary = false,
                                                               .draft_unmodified = false});
  assert_equal("not at boundary",
               blocked.state.text,
               "history recall should not steal Up when the cursor is not at recall boundary");
  assert_equal("editing",
               blocked.state.mode,
               "failed recall should keep the composer in editing mode");
}

void composer_reverse_search_cycles_matching_prompts() {
  TuiComposer composer;
  submit_prompt(composer, "build ready");
  submit_prompt(composer, "check status");
  submit_prompt(composer, "build docs");

    static_cast<void>(
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged, .text = "build"}));
    const auto first_match =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::CtrlR, .text = ""});
  assert_equal("reverse-search",
               first_match.state.mode,
               "Ctrl+R should switch the composer into reverse-search mode");
  assert_true(first_match.state.history_query.has_value(),
              "reverse-search should preserve the current query string");
  assert_equal("build",
               *first_match.state.history_query,
               "reverse-search should use the current draft as the history query");
  assert_equal("build docs",
               first_match.state.text,
               "reverse-search should start from the newest matching prompt");

  const auto second_match =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::CtrlR, .text = ""});
  assert_equal("build ready",
               second_match.state.text,
               "repeated Ctrl+R should continue scanning older matching prompts");

  const auto exit_search =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged,
                                              .text = "fresh draft after search"});
  assert_equal("editing",
               exit_search.state.mode,
               "editing after reverse-search should restore the composer to editing mode");
  assert_true(!exit_search.state.history_query.has_value(),
              "editing after reverse-search should clear the active history query");
}

void input_history_header_avoids_owner_private_includes_and_io() {
  const std::string input_history_header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_INPUT_HISTORY_HEADER});

  assert_true(input_history_header_text.find("access/") == std::string::npos,
              "input history should not include access private headers");
  assert_true(input_history_header_text.find("runtime/") == std::string::npos,
              "input history should not include runtime private headers");
  assert_true(input_history_header_text.find("llm/") == std::string::npos,
              "input history should not include llm private headers");
  assert_true(input_history_header_text.find("profiles/") == std::string::npos,
              "input history should not include profile private headers");
  assert_true(input_history_header_text.find("ftxui") == std::string::npos,
              "input history should not leak renderer framework types");
  assert_true(input_history_header_text.find("iostream") == std::string::npos,
              "input history should not perform stream I/O");
  assert_true(input_history_header_text.find("fstream") == std::string::npos,
              "input history should not depend on file I/O");
  assert_true(input_history_header_text.find("filesystem") == std::string::npos,
              "input history should not depend on filesystem APIs");
}

}  // namespace

int main() {
  try {
    input_history_tracks_non_blank_entries_and_reverse_matches();
    composer_recalls_history_only_at_boundary_and_restores_seed_draft();
    composer_reverse_search_cycles_matching_prompts();
    input_history_header_avoids_owner_private_includes_and_io();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiComposerHistoryTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}