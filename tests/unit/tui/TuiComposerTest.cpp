#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "support/TestAssertions.h"
#include "view/TuiComposer.h"

#ifndef DASALL_TUI_COMPOSER_HEADER
#define DASALL_TUI_COMPOSER_HEADER "/home/gangan/DASALL/apps/tui/src/view/TuiComposer.h"
#endif

#ifndef DASALL_TUI_COMPOSER_IMPL
#define DASALL_TUI_COMPOSER_IMPL "/home/gangan/DASALL/apps/tui/src/view/TuiComposer.cpp"
#endif

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

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void composer_handles_multiline_submit_busy_and_external_editor_transitions() {
  TuiComposer composer;

  const auto editing =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged,
                                              .text = "Draft line"});
  assert_equal("editing",
               editing.state.mode,
               "text change should move the composer into editing mode");
  assert_equal("Draft line",
               editing.state.text,
               "text change should replace the current composer draft");
  assert_true(editing.state.dirty,
              "text change should mark the draft as dirty");
  assert_equal(static_cast<int>(TuiComposerActionType::None),
               static_cast<int>(editing.action.type),
               "plain editing should not emit a side effect action");

  const auto multiline = composer.handle_key(
      TuiComposerKeyEvent{.key = TuiComposerKey::AltEnter, .text = ""});
  assert_equal("Draft line\n",
               multiline.state.text,
               "Alt+Enter should append a newline instead of submitting the draft");

    const auto multiline_again = composer.handle_key(
      TuiComposerKeyEvent{.key = TuiComposerKey::CtrlJ, .text = ""});
  assert_equal("Draft line\n\n",
               multiline_again.state.text,
               "Ctrl+J should share the multiline newline behavior");

  static_cast<void>(composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged,
                                                            .text = "submit this turn"}));
  const auto submitted =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Enter, .text = ""});
  assert_equal(static_cast<int>(TuiComposerActionType::SubmitRequested),
               static_cast<int>(submitted.action.type),
               "Enter should emit a submit request when a draft is present");
  assert_equal("submit this turn",
               submitted.action.text,
               "submit request should preserve the outgoing draft text");
  assert_equal("submitting",
               submitted.state.mode,
               "submit should move the composer into submitting mode");
  assert_true(submitted.state.text.empty(),
              "submit should clear the local draft buffer for the next busy draft");
  assert_true(!submitted.state.can_submit,
              "submit should disable repeated submit while the turn is in-flight");
  assert_true(!submitted.state.dirty,
              "submit should clear the dirty flag after emitting the turn request");

  const auto busy_state = composer.set_busy(true);
  assert_equal("pending-interaction",
               busy_state.mode,
               "busy mode should move the composer into pending interaction state");
  assert_true(!busy_state.can_submit,
              "busy mode should keep submit disabled");

  const auto busy_edit = composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::TextChanged,
                                                                 .text = "draft while waiting"});
  assert_equal("pending-interaction",
               busy_edit.state.mode,
               "busy editing should preserve the pending interaction mode");
  assert_equal("draft while waiting",
               busy_edit.state.text,
               "busy editing should keep the new draft visible");
  assert_true(!busy_edit.state.can_submit,
              "busy editing should continue to block repeated submit");

  const auto blocked_submit =
      composer.handle_key(TuiComposerKeyEvent{.key = TuiComposerKey::Enter, .text = ""});
  assert_equal(static_cast<int>(TuiComposerActionType::None),
               static_cast<int>(blocked_submit.action.type),
               "busy drafts should not emit a second submit request");
  assert_equal("pending-interaction",
               blocked_submit.state.mode,
               "blocked busy submit should keep the pending interaction mode");

  const auto ready_state = composer.set_busy(false);
  assert_equal("editing",
               ready_state.mode,
               "leaving busy mode should restore normal editing when a draft exists");
  assert_true(ready_state.can_submit,
              "leaving busy mode should re-enable submit");

  const auto editor_request = composer.open_external_editor();
  assert_equal(static_cast<int>(TuiComposerActionType::OpenExternalEditorRequested),
               static_cast<int>(editor_request.action.type),
               "open_external_editor should request an external editor action");
  assert_equal("draft while waiting",
               editor_request.action.text,
               "external editor request should preserve the current draft payload");
  assert_equal("external-editor",
               editor_request.state.mode,
               "opening the external editor should switch the composer mode");

  const auto cancelled = composer.apply_external_editor_result(std::nullopt);
  assert_equal("editing",
               cancelled.mode,
               "cancelled external editor should restore the previous editing mode");
  assert_equal("draft while waiting",
               cancelled.text,
               "cancelled external editor should preserve the original draft");

  static_cast<void>(composer.open_external_editor());
  const auto edited_externally =
      composer.apply_external_editor_result(std::string{"edited externally"});
  assert_equal("editing",
               edited_externally.mode,
               "successful external editor return should restore editing mode");
  assert_equal("edited externally",
               edited_externally.text,
               "external editor result should replace the local composer draft");
}

void composer_files_avoid_owner_private_includes_and_renderer_io() {
  const std::string composer_header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_COMPOSER_HEADER});
  const std::string composer_impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_COMPOSER_IMPL});
  const std::string input_history_header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_INPUT_HISTORY_HEADER});

  for (const std::string* file_text :
       {&composer_header_text, &composer_impl_text, &input_history_header_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "composer files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "composer files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "composer files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "composer files should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "composer files should not leak renderer framework types");
    assert_true(file_text->find("iostream") == std::string::npos,
                "composer production files should not perform stream I/O");
    assert_true(file_text->find("fstream") == std::string::npos,
                "composer production files should not depend on file I/O");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "composer production files should not depend on filesystem APIs");
    assert_true(file_text->find("chrono") == std::string::npos,
                "composer production files should not read system time directly");
  }
}

}  // namespace

int main() {
  try {
    composer_handles_multiline_submit_busy_and_external_editor_transitions();
    composer_files_avoid_owner_private_includes_and_renderer_io();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiComposerTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}