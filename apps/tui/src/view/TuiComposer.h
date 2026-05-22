#pragma once

#include <optional>
#include <string>

#include "model/TuiScreenModel.h"
#include "view/TuiInputHistory.h"

namespace dasall::tui::view {

enum class TuiComposerKey {
  TextChanged,
  Enter,
  AltEnter,
  CtrlJ,
  Up,
  Down,
  CtrlR,
};

struct TuiComposerKeyEvent {
  TuiComposerKey key = TuiComposerKey::TextChanged;
  std::string text;
  bool cursor_at_boundary = false;
  bool draft_unmodified = true;
};

enum class TuiComposerActionType {
  None,
  SubmitRequested,
  OpenExternalEditorRequested,
};

struct TuiComposerAction {
  TuiComposerActionType type = TuiComposerActionType::None;
  std::string text;
};

struct TuiComposerUpdate {
  model::TuiComposerState state;
  TuiComposerAction action;
};

class TuiComposer {
 public:
  explicit TuiComposer(model::TuiComposerState state = {});

  [[nodiscard]] const model::TuiComposerState& state() const noexcept;

  [[nodiscard]] const TuiInputHistory& history() const noexcept;

  [[nodiscard]] TuiComposerUpdate handle_key(const TuiComposerKeyEvent& event);

  [[nodiscard]] model::TuiComposerState set_busy(bool busy);

  [[nodiscard]] model::TuiComposerState recall_history(int direction);

  [[nodiscard]] TuiComposerUpdate open_external_editor();

  [[nodiscard]] model::TuiComposerState apply_external_editor_result(
      const std::optional<std::string>& edited_text);

 private:
  void clear_navigation_state();
  void apply_text_change(std::string text);
  void apply_idle_mode();

  [[nodiscard]] bool can_recall_from_key(const TuiComposerKeyEvent& event) const;
  [[nodiscard]] bool has_draft_to_submit() const;
  [[nodiscard]] model::TuiComposerState enter_reverse_search();
  [[nodiscard]] TuiComposerUpdate snapshot(TuiComposerAction action = {}) const;

  model::TuiComposerState state_;
  TuiInputHistory history_;
  bool busy_ = false;
  std::optional<std::size_t> history_index_;
  std::optional<std::string> recall_seed_;
  std::optional<std::size_t> reverse_search_index_;
  std::optional<model::TuiComposerState> external_editor_checkpoint_;
};

}  // namespace dasall::tui::view