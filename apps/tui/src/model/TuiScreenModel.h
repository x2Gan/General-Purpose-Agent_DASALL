#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "model/TuiAction.h"

namespace dasall::tui::model {

struct TuiMessageView {
  std::string role;
  std::string content;
  std::string timestamp;
  std::vector<std::string> badges;
  bool collapsible = false;
  bool collapsed = false;
};

struct TuiComposerState {
  std::string text;
  std::size_t cursor_offset = 0;
  std::string mode = "ready";
  std::optional<std::string> history_query;
  bool can_submit = true;
  bool dirty = false;
  bool cursor_visible = true;
  std::string activity_indicator;
};

struct TuiScreenModel {
  data::TuiSessionView session;
  std::vector<TuiMessageView> transcript;
  std::size_t transcript_scroll_offset = 0;
  bool transcript_follow_tail = true;
  data::TuiStatusProjection status;
  data::TuiModelRouteProjection route;
  TuiComposerState composer;
  TuiFocusState focus = TuiFocusState::Composer;
  std::vector<TuiBanner> banners;
  TuiModalState modal;
  std::string debug_reason;
};

}  // namespace dasall::tui::model