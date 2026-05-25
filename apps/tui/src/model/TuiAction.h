#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "data/TuiProjectionTypes.h"

namespace dasall::tui::model {

enum class TuiFocusState {
  Transcript,
  Composer,
  Selector,
  StatusPanel,
  Modal,
};

enum class TuiBannerLevel {
  Info,
  Warning,
  Error,
};

struct TuiBanner {
  TuiBannerLevel level = TuiBannerLevel::Info;
  std::string title;
  std::string message;
  std::optional<std::string> reason_code;
  bool sticky = false;
};

enum class TuiModalKind {
  None,
  Selector,
  Confirm,
  Help,
  Session,
};

struct TuiModalState {
  TuiModalKind kind = TuiModalKind::None;
  std::string title;
  std::string body;
  std::vector<std::string> actions;
  std::optional<std::size_t> selected_action_index;
};

enum class TuiActionType {
  Noop,
  FocusChanged,
  BannerAdded,
  BannerCleared,
  ModalShown,
  ModalHidden,
  SessionHydrated,
  StatusUpdated,
  RouteUpdated,
  EventAppended,
  ComposerTextChanged,
  ComposerModeChanged,
  ComposerSubmitAvailabilityChanged,
  StatusQueryRequested,
  SessionQueryRequested,
  ForegroundSessionResetApplied,
  ForegroundSessionClearRequested,
  TurnSubmitRequested,
  ExitRequested,
};

struct TuiAction {
  TuiActionType type = TuiActionType::Noop;
  std::string debug_reason;
  std::optional<TuiFocusState> focus;
  std::optional<TuiBanner> banner;
  std::optional<TuiModalState> modal;
  std::optional<data::TuiSessionView> session;
  std::optional<data::TuiStatusProjection> status;
  std::optional<data::TuiModelRouteProjection> route;
  std::optional<data::TuiEventProjection> event;
  std::optional<std::string> composer_text;
  std::optional<std::string> composer_mode;
  std::optional<bool> composer_can_submit;
};

}  // namespace dasall::tui::model