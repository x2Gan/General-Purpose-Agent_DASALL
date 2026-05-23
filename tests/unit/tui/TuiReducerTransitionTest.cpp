#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "model/TuiReducer.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_REDUCER_HEADER
#define DASALL_TUI_REDUCER_HEADER "/home/gangan/DASALL/apps/tui/src/model/TuiReducer.h"
#endif

#ifndef DASALL_TUI_REDUCER_IMPL
#define DASALL_TUI_REDUCER_IMPL "/home/gangan/DASALL/apps/tui/src/model/TuiReducer.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::model::TuiAction;
using dasall::tui::model::TuiActionType;
using dasall::tui::model::TuiBanner;
using dasall::tui::model::TuiBannerLevel;
using dasall::tui::model::TuiFocusState;
using dasall::tui::model::TuiScreenModel;
using dasall::tui::model::reduce;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void reducer_handles_submit_transition_via_composer_mode() {
  TuiScreenModel model;
  model.composer.text = "hello reducer";
  model.composer.dirty = true;

  TuiAction action;
  action.type = TuiActionType::ComposerModeChanged;
  action.composer_mode = std::string{"submitting"};
  action.debug_reason = "submit requested";

  const TuiScreenModel result = reduce(model, action);

  assert_equal("submitting",
               result.composer.mode,
               "submit path should switch composer into submitting mode");
  assert_true(!result.composer.can_submit,
              "submit path should disable repeated submit while current turn is in-flight");
  assert_true(!result.composer.dirty,
              "submit path should clear dirty state once reducer has accepted the submit");
  assert_equal(static_cast<int>(TuiFocusState::Transcript),
               static_cast<int>(result.focus),
               "submit path should move focus to transcript for receipt/event updates");
  assert_equal("submit requested",
               result.debug_reason,
               "submit path should preserve the latest debug reason for tracing");
}

void reducer_appends_event_projection_and_refreshes_status() {
  TuiScreenModel model;
  model.status.stage = "idle";

  TuiStatusProjection status;
  status.stage = "responding";
  status.health_summary = "healthy";

  TuiTurnReceipt receipt;
  receipt.disposition = "accepted_async";
  receipt.summary_text = "assistant reply queued";

  TuiEventProjection event;
  event.event_kind = "turn_receipt";
  event.timestamp = "2026-05-22T12:00:00Z";
  event.status_delta = status;
  event.turn_receipt = receipt;

  TuiAction action;
  action.type = TuiActionType::EventAppended;
  action.event = event;

  const TuiScreenModel result = reduce(model, action);

  assert_equal(1,
               static_cast<int>(result.transcript.size()),
               "event append should add one transcript row");
  assert_equal("assistant",
               result.transcript.front().role,
               "turn receipt events should project as assistant-facing transcript rows");
  assert_equal("assistant reply queued",
               result.transcript.front().content,
               "turn receipt events should preserve receipt summary text");
  assert_equal("2026-05-22T12:00:00Z",
               result.transcript.front().timestamp,
               "event append should preserve event timestamp on the transcript row");
  assert_equal("responding",
               result.status.stage,
               "event append should refresh status from event status_delta");
}

void reducer_switches_focus_and_manages_banners() {
  TuiScreenModel model;

  TuiAction focus_action;
  focus_action.type = TuiActionType::FocusChanged;
  focus_action.focus = TuiFocusState::StatusPanel;

  TuiScreenModel focused = reduce(model, focus_action);
  assert_equal(static_cast<int>(TuiFocusState::StatusPanel),
               static_cast<int>(focused.focus),
               "focus change should switch to the requested non-modal focus region");

  TuiBanner banner;
  banner.level = TuiBannerLevel::Warning;
  banner.title = "Latency notice";
  banner.message = "render degraded";

  TuiAction add_banner_action;
  add_banner_action.type = TuiActionType::BannerAdded;
  add_banner_action.banner = banner;

  TuiScreenModel with_banner = reduce(focused, add_banner_action);
  assert_equal(1,
               static_cast<int>(with_banner.banners.size()),
               "banner add should append a user-visible banner");
  assert_equal("Latency notice",
               with_banner.banners.front().title,
               "banner add should preserve banner title");

  TuiAction clear_banner_action;
  clear_banner_action.type = TuiActionType::BannerCleared;

  TuiScreenModel cleared = reduce(with_banner, clear_banner_action);
  assert_true(cleared.banners.empty(),
              "banner clear should remove all currently visible banners");
}

void reducer_resets_foreground_session_state_for_clear() {
  TuiScreenModel model;
  model.session.session_id = "session-clear-001";
  model.session.profile_id = "desktop_full";
  model.status.stage = "tool_calling";
  model.route.current_provider_id = "provider-clear";
  model.route.current_model_id = "model-clear";
  model.transcript.push_back(dasall::tui::model::TuiMessageView{
      .role = "assistant",
      .content = "stale transcript",
      .timestamp = "2026-05-23T18:26:00Z",
  });
  model.composer.text = "stale draft";
  model.composer.mode = "editing";
  model.composer.can_submit = true;
  model.composer.dirty = true;
  model.focus = TuiFocusState::Modal;

  TuiBanner banner;
  banner.level = TuiBannerLevel::Warning;
  banner.title = "old banner";
  banner.message = "clear me";
  model.banners.push_back(banner);
  model.modal.kind = dasall::tui::model::TuiModalKind::Help;
  model.modal.title = "Open modal";

  TuiAction action;
  action.type = TuiActionType::ForegroundSessionResetApplied;
  action.debug_reason = "foreground_session_reset_applied";

  const TuiScreenModel result = reduce(model, action);

  assert_true(result.session.session_id.empty(),
              "foreground session reset should unbind the previous session projection");
  assert_true(result.transcript.empty(),
              "foreground session reset should clear the prior transcript view");
  assert_true(result.status.stage.empty(),
              "foreground session reset should clear the prior status projection");
  assert_true(result.route.current_provider_id.empty() &&
                  result.route.current_model_id.empty(),
              "foreground session reset should clear the prior route projection");
  assert_true(result.banners.empty(),
              "foreground session reset should clear prior banners before the next session binds");
  assert_equal(static_cast<int>(dasall::tui::model::TuiModalKind::None),
               static_cast<int>(result.modal.kind),
               "foreground session reset should hide any active modal");
  assert_equal(std::string("ready"),
               result.composer.mode,
               "foreground session reset should restore the composer to ready mode");
  assert_true(result.composer.text.empty() && !result.composer.dirty,
              "foreground session reset should clear the in-flight composer draft");
  assert_true(result.composer.can_submit,
              "foreground session reset should make the composer submittable again");
  assert_equal(static_cast<int>(TuiFocusState::Composer),
               static_cast<int>(result.focus),
               "foreground session reset should return focus to the composer");
  assert_equal("foreground_session_reset_applied",
               result.debug_reason,
               "foreground session reset should preserve the reducer debug reason");
}

void reducer_unknown_action_is_noop_and_records_debug_reason() {
  TuiScreenModel model;
  model.focus = TuiFocusState::Selector;

  TuiAction action;
  action.type = static_cast<TuiActionType>(999);
  action.debug_reason = "unsupported test action";

  const TuiScreenModel result = reduce(model, action);

  assert_equal(static_cast<int>(TuiFocusState::Selector),
               static_cast<int>(result.focus),
               "unknown reducer action should keep existing focus unchanged");
  assert_true(result.transcript.empty() && result.banners.empty(),
              "unknown reducer action should remain a no-op for visible UI slices");
  assert_equal("unsupported test action",
               result.debug_reason,
               "unknown reducer action should still preserve debug reason for diagnostics");
}

void reducer_invalid_modal_focus_fails_closed_to_error_banner() {
  TuiScreenModel model;

  TuiAction action;
  action.type = TuiActionType::FocusChanged;
  action.focus = TuiFocusState::Modal;
  action.debug_reason = "missing modal state";

  const TuiScreenModel result = reduce(model, action);

  assert_equal(static_cast<int>(TuiFocusState::Composer),
               static_cast<int>(result.focus),
               "invalid modal focus transition should preserve the previous focus");
  assert_equal(1,
               static_cast<int>(result.banners.size()),
               "invalid modal focus transition should emit an error banner");
  assert_equal(static_cast<int>(TuiBannerLevel::Error),
               static_cast<int>(result.banners.front().level),
               "invalid modal focus transition should fail-closed as an error banner");
  assert_true(result.banners.front().reason_code.has_value(),
              "invalid modal focus transition should expose a stable reason code");
  assert_equal("invalid_focus_transition",
               *result.banners.front().reason_code,
               "invalid modal focus transition should keep the frozen reason code");
  assert_equal("missing modal state",
               result.debug_reason,
               "invalid modal focus transition should preserve debug reason for tracing");
}

void reducer_files_avoid_renderer_io_and_owner_private_includes() {
  const std::string reducer_header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_REDUCER_HEADER});
  const std::string reducer_impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_REDUCER_IMPL});

  for (const std::string* reducer_text : {&reducer_header_text, &reducer_impl_text}) {
    assert_true(reducer_text->find("access/") == std::string::npos,
                "TuiReducer files should not include access private headers");
    assert_true(reducer_text->find("runtime/") == std::string::npos,
                "TuiReducer files should not include runtime private headers");
    assert_true(reducer_text->find("llm/") == std::string::npos,
                "TuiReducer files should not include llm private headers");
    assert_true(reducer_text->find("profiles/") == std::string::npos,
                "TuiReducer files should not include profile private headers");
    assert_true(reducer_text->find("ftxui") == std::string::npos,
                "TuiReducer files should not leak FTXUI into reducer implementation");
    assert_true(reducer_text->find("iostream") == std::string::npos,
                "TuiReducer files should not do stream I/O");
    assert_true(reducer_text->find("fstream") == std::string::npos,
                "TuiReducer files should not depend on file I/O");
    assert_true(reducer_text->find("filesystem") == std::string::npos,
                "TuiReducer files should not depend on filesystem APIs in production code");
    assert_true(reducer_text->find("chrono") == std::string::npos,
                "TuiReducer files should not read system time directly");
    assert_true(reducer_text->find("AgentRequest") == std::string::npos,
                "TuiReducer files should not bind to access request owners");
    assert_true(reducer_text->find("RuntimeDispatchRequest") == std::string::npos,
                "TuiReducer files should not bind to runtime dispatch owners");
  }
}

}  // namespace

int main() {
  try {
    reducer_handles_submit_transition_via_composer_mode();
    reducer_appends_event_projection_and_refreshes_status();
    reducer_switches_focus_and_manages_banners();
    reducer_resets_foreground_session_state_for_clear();
    reducer_unknown_action_is_noop_and_records_debug_reason();
    reducer_invalid_modal_focus_fails_closed_to_error_banner();
    reducer_files_avoid_renderer_io_and_owner_private_includes();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiReducerTransitionTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}