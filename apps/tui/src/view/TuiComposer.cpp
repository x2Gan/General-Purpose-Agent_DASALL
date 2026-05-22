#include "view/TuiComposer.h"

#include <string_view>
#include <utility>

namespace dasall::tui::view {
namespace {

[[nodiscard]] bool has_visible_content(std::string_view text) noexcept {
  for (const char character : text) {
    if (character != ' ' && character != '\t' && character != '\n' &&
        character != '\r') {
      return true;
    }
  }

  return false;
}

}  // namespace

TuiComposer::TuiComposer(model::TuiComposerState state) : state_(std::move(state)) {
  busy_ = state_.mode == "pending-interaction";
}

const model::TuiComposerState& TuiComposer::state() const noexcept { return state_; }

const TuiInputHistory& TuiComposer::history() const noexcept { return history_; }

TuiComposerUpdate TuiComposer::handle_key(const TuiComposerKeyEvent& event) {
  if (state_.mode == "external-editor") {
    return snapshot();
  }

  switch (event.key) {
    case TuiComposerKey::TextChanged:
      if (state_.mode == "submitting") {
        return snapshot();
      }

      clear_navigation_state();
      apply_text_change(event.text);
      return snapshot();

    case TuiComposerKey::Enter: {
      if (state_.mode == "submitting" || !state_.can_submit || !has_draft_to_submit()) {
        return snapshot();
      }

      history_.record(state_.text);
      clear_navigation_state();

      TuiComposerAction action;
      action.type = TuiComposerActionType::SubmitRequested;
      action.text = state_.text;

      state_.text.clear();
      state_.mode = "submitting";
      state_.can_submit = false;
      state_.dirty = false;
      return snapshot(std::move(action));
    }

    case TuiComposerKey::AltEnter:
    case TuiComposerKey::CtrlJ:
      if (state_.mode == "submitting") {
        return snapshot();
      }

      clear_navigation_state();
      apply_text_change(state_.text + "\n");
      return snapshot();

    case TuiComposerKey::Up:
      if (state_.mode == "submitting" || !can_recall_from_key(event)) {
        return snapshot();
      }

      static_cast<void>(recall_history(-1));
      return snapshot();

    case TuiComposerKey::Down:
      if (state_.mode != "history-recall") {
        return snapshot();
      }

      static_cast<void>(recall_history(1));
      return snapshot();

    case TuiComposerKey::CtrlR:
      if (state_.mode == "submitting") {
        return snapshot();
      }

      static_cast<void>(enter_reverse_search());
      return snapshot();
  }

  return snapshot();
}

model::TuiComposerState TuiComposer::set_busy(bool busy) {
  busy_ = busy;
  state_.can_submit = !busy_;

  if (busy_) {
    clear_navigation_state();
    state_.mode = "pending-interaction";
    state_.dirty = !state_.text.empty();
    return state_;
  }

  apply_idle_mode();
  return state_;
}

model::TuiComposerState TuiComposer::recall_history(int direction) {
  if (history_.empty()) {
    return state_;
  }

  reverse_search_index_.reset();
  state_.history_query.reset();

  if (direction < 0) {
    if (!history_index_.has_value()) {
      recall_seed_ = state_.text;
    }

    history_index_ = history_.older(history_index_);
    if (!history_index_.has_value()) {
      return state_;
    }

    state_.text = history_.at(*history_index_);
    state_.mode = "history-recall";
    state_.can_submit = !busy_;
    state_.dirty = false;
    return state_;
  }

  if (!history_index_.has_value()) {
    return state_;
  }

  const auto newer_index = history_.newer(history_index_);
  if (!newer_index.has_value()) {
    history_index_.reset();
    state_.text = recall_seed_.value_or(std::string{});
    recall_seed_.reset();
    state_.dirty = !state_.text.empty();
    apply_idle_mode();
    return state_;
  }

  history_index_ = newer_index;
  state_.text = history_.at(*history_index_);
  state_.mode = "history-recall";
  state_.can_submit = !busy_;
  state_.dirty = false;
  return state_;
}

TuiComposerUpdate TuiComposer::open_external_editor() {
  if (state_.mode == "submitting" || state_.mode == "external-editor") {
    return snapshot();
  }

  external_editor_checkpoint_ = state_;
  clear_navigation_state();
  state_.mode = "external-editor";
  state_.can_submit = false;

  TuiComposerAction action;
  action.type = TuiComposerActionType::OpenExternalEditorRequested;
  action.text = external_editor_checkpoint_->text;
  return snapshot(std::move(action));
}

model::TuiComposerState TuiComposer::apply_external_editor_result(
    const std::optional<std::string>& edited_text) {
  if (!external_editor_checkpoint_.has_value()) {
    return state_;
  }

  state_ = *external_editor_checkpoint_;
  external_editor_checkpoint_.reset();

  if (!edited_text.has_value()) {
    state_.can_submit = !busy_;
    if (busy_) {
      state_.mode = "pending-interaction";
    }
    return state_;
  }

  clear_navigation_state();
  apply_text_change(*edited_text);
  return state_;
}

void TuiComposer::clear_navigation_state() {
  history_index_.reset();
  recall_seed_.reset();
  reverse_search_index_.reset();
  state_.history_query.reset();
}

void TuiComposer::apply_text_change(std::string text) {
  state_.text = std::move(text);
  state_.dirty = !state_.text.empty();
  state_.can_submit = !busy_;
  apply_idle_mode();
}

void TuiComposer::apply_idle_mode() {
  if (busy_) {
    state_.mode = "pending-interaction";
    return;
  }

  state_.mode = state_.text.empty() ? "ready" : "editing";
}

bool TuiComposer::can_recall_from_key(const TuiComposerKeyEvent& event) const {
  if (state_.mode == "history-recall") {
    return true;
  }

  return state_.text.empty() || (event.cursor_at_boundary && event.draft_unmodified);
}

bool TuiComposer::has_draft_to_submit() const {
  return has_visible_content(state_.text);
}

model::TuiComposerState TuiComposer::enter_reverse_search() {
  if (history_.empty()) {
    return state_;
  }

  const std::string query = state_.history_query.value_or(state_.text);
  const std::optional<std::size_t> before =
      state_.mode == "reverse-search" ? reverse_search_index_ : std::nullopt;
  const auto match = history_.latest_match(query, before);

  history_index_.reset();
  recall_seed_.reset();
  state_.history_query = query;
  state_.mode = "reverse-search";
  state_.can_submit = !busy_;

  if (!match.has_value()) {
    reverse_search_index_.reset();
    state_.dirty = !state_.text.empty();
    return state_;
  }

  reverse_search_index_ = match;
  state_.text = history_.at(*match);
  state_.dirty = false;
  return state_;
}

TuiComposerUpdate TuiComposer::snapshot(TuiComposerAction action) const {
  TuiComposerUpdate update;
  update.state = state_;
  update.action = std::move(action);
  return update;
}

}  // namespace dasall::tui::view