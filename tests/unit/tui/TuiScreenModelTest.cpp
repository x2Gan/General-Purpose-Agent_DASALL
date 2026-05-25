#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "model/TuiAction.h"
#include "model/TuiScreenModel.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_ACTION_HEADER
#define DASALL_TUI_ACTION_HEADER "/home/gangan/DASALL/apps/tui/src/model/TuiAction.h"
#endif

#ifndef DASALL_TUI_SCREEN_MODEL_HEADER
#define DASALL_TUI_SCREEN_MODEL_HEADER "/home/gangan/DASALL/apps/tui/src/model/TuiScreenModel.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::model::TuiAction;
using dasall::tui::model::TuiActionType;
using dasall::tui::model::TuiBanner;
using dasall::tui::model::TuiBannerLevel;
using dasall::tui::model::TuiComposerState;
using dasall::tui::model::TuiFocusState;
using dasall::tui::model::TuiMessageView;
using dasall::tui::model::TuiModalKind;
using dasall::tui::model::TuiModalState;
using dasall::tui::model::TuiScreenModel;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void screen_model_and_action_expose_expected_default_shape() {
  const TuiAction action;
  const TuiBanner banner;
  const TuiModalState modal;
  const TuiMessageView message;
  const TuiComposerState composer;
  const TuiScreenModel model;

  assert_equal(static_cast<int>(TuiActionType::Noop),
               static_cast<int>(action.type),
               "TuiAction should default to Noop");
  assert_true(action.debug_reason.empty(),
              "TuiAction should not carry a debug reason by default");
  assert_true(!action.focus.has_value() && !action.banner.has_value() &&
                  !action.modal.has_value() && !action.session.has_value() &&
                  !action.status.has_value() && !action.route.has_value() &&
                  !action.event.has_value() && !action.composer_text.has_value() &&
                  !action.composer_mode.has_value() &&
                  !action.composer_can_submit.has_value(),
              "TuiAction should keep all optional payloads unset by default");

  assert_equal(static_cast<int>(TuiBannerLevel::Info),
               static_cast<int>(banner.level),
               "TuiBanner should default to info level");
  assert_true(banner.title.empty() && banner.message.empty() &&
                  !banner.reason_code.has_value() && !banner.sticky,
              "TuiBanner should remain a plain optional notice by default");

  assert_equal(static_cast<int>(TuiModalKind::None),
               static_cast<int>(modal.kind),
               "TuiModalState should default to no modal");
  assert_true(modal.title.empty() && modal.body.empty() && modal.actions.empty() &&
                  !modal.selected_action_index.has_value(),
              "TuiModalState should start empty when no modal is shown");

  assert_true(message.role.empty() && message.content.empty() &&
                  message.timestamp.empty() && message.badges.empty() &&
                  !message.collapsible && !message.collapsed,
              "TuiMessageView should default to an empty renderable transcript row");

  assert_true(composer.text.empty() && composer.mode == "ready" &&
                  !composer.history_query.has_value() && composer.can_submit &&
                  !composer.dirty && composer.cursor_visible &&
                  composer.cursor_offset == 0U && composer.activity_indicator.empty(),
          "TuiComposerState should start ready with submit enabled and no activity indicator");

  assert_equal(static_cast<int>(TuiFocusState::Composer),
               static_cast<int>(model.focus),
               "TuiScreenModel should default focus to the composer");
  assert_true(model.session.session_id.empty() && model.transcript.empty() &&
            model.transcript_scroll_offset == 0U && model.transcript_follow_tail &&
                  model.status.stage.empty() && model.route.current_model_id.empty() &&
                  model.composer.mode == "ready" && model.banners.empty() &&
                  model.debug_reason.empty(),
              "TuiScreenModel should compose DTO defaults into an empty first-frame model");
  assert_equal(static_cast<int>(TuiModalKind::None),
               static_cast<int>(model.modal.kind),
               "TuiScreenModel should start without an active modal");
  assert_true(model.route.next_preference.applies_to_next_turn_only,
              "TuiScreenModel should preserve the next-turn-only route preference contract");
}

void screen_model_headers_avoid_renderer_io_and_owner_private_includes() {
  const std::string action_header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_ACTION_HEADER});
  const std::string screen_model_header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_SCREEN_MODEL_HEADER});

  for (const std::string* header_text : {&action_header_text, &screen_model_header_text}) {
    assert_true(header_text->find("access/") == std::string::npos,
                "TUI model headers should not include access private headers");
    assert_true(header_text->find("runtime/") == std::string::npos,
                "TUI model headers should not include runtime private headers");
    assert_true(header_text->find("llm/") == std::string::npos,
                "TUI model headers should not include llm private headers");
    assert_true(header_text->find("profiles/") == std::string::npos,
                "TUI model headers should not include profile private headers");
    assert_true(header_text->find("ftxui") == std::string::npos,
                "TUI model headers should not leak FTXUI into model/action definitions");
    assert_true(header_text->find("iostream") == std::string::npos,
                "TUI model headers should not do stream I/O");
    assert_true(header_text->find("fstream") == std::string::npos,
                "TUI model headers should not depend on file I/O");
    assert_true(header_text->find("filesystem") == std::string::npos,
                "TUI model headers should not depend on filesystem I/O");
    assert_true(header_text->find("chrono") == std::string::npos,
                "TUI model headers should not read system time directly");
    assert_true(header_text->find("AgentRequest") == std::string::npos,
                "TUI model headers should not bind to access request owners");
    assert_true(header_text->find("RuntimeDispatchRequest") == std::string::npos,
                "TUI model headers should not bind to runtime dispatch owners");
  }
}

}  // namespace

int main() {
  try {
    screen_model_and_action_expose_expected_default_shape();
    screen_model_headers_avoid_renderer_io_and_owner_private_includes();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiScreenModelTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}