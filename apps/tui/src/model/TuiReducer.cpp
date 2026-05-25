#include "model/TuiReducer.h"

#include <string>
#include <utility>

namespace dasall::tui::model {
namespace {

[[nodiscard]] std::string resolve_debug_reason(const TuiAction& action,
                                               const std::string& fallback) {
  return action.debug_reason.empty() ? fallback : action.debug_reason;
}

void append_banner(TuiScreenModel& model, TuiBanner banner) {
  model.banners.push_back(std::move(banner));
}

void fail_closed(TuiScreenModel& model,
                 const TuiAction& action,
                 const std::string& reason_code,
                 const std::string& message) {
  model.debug_reason = resolve_debug_reason(action, reason_code);

  TuiBanner banner;
  banner.level = TuiBannerLevel::Error;
  banner.title = "Reducer rejected action";
  banner.message = message;
  banner.reason_code = reason_code;
  append_banner(model, std::move(banner));
}

[[nodiscard]] TuiMessageView build_message_view(const data::TuiEventProjection& event) {
  TuiMessageView message;
  message.timestamp = event.timestamp;

  if (!event.event_kind.empty()) {
    message.badges.push_back(event.event_kind);
  }

  if (event.turn_receipt.has_value()) {
    const auto& receipt = *event.turn_receipt;
    message.role = "assistant";
    message.content = receipt.summary_text.empty() ? receipt.disposition : receipt.summary_text;

    if (!receipt.disposition.empty()) {
      message.badges.push_back(receipt.disposition);
    }
    if (receipt.reason_code.has_value()) {
      message.badges.push_back(*receipt.reason_code);
    }
    return message;
  }

  if (event.tool_summary.has_value()) {
    const auto& tool_summary = *event.tool_summary;
    message.role = "tool";
    if (!tool_summary.observation_summary.empty()) {
      message.content = tool_summary.observation_summary;
    } else if (!tool_summary.risk_summary.empty()) {
      message.content = tool_summary.risk_summary;
    } else {
      message.content = tool_summary.tool_name;
    }

    if (!tool_summary.tool_name.empty()) {
      message.badges.push_back(tool_summary.tool_name);
    }
    for (const auto& badge : tool_summary.badges) {
      message.badges.push_back(badge);
    }
    return message;
  }

  message.role = "system";
  if (event.banner_reason.has_value()) {
    message.content = *event.banner_reason;
  } else {
    message.content = event.event_kind;
  }

  return message;
}

void clear_debug_reason_if_unspecified(TuiScreenModel& model, const TuiAction& action) {
  model.debug_reason = action.debug_reason;
}

}  // namespace

TuiScreenModel reduce(TuiScreenModel current, TuiAction action) {
  switch (action.type) {
    case TuiActionType::Noop:
      if (!action.debug_reason.empty()) {
        current.debug_reason = action.debug_reason;
      }
      return current;

    case TuiActionType::FocusChanged:
      if (!action.focus.has_value()) {
        fail_closed(current,
                    action,
                    "missing_focus_payload",
                    "FocusChanged requires a focus payload");
        return current;
      }
      if (*action.focus == TuiFocusState::Modal &&
          current.modal.kind == TuiModalKind::None) {
        fail_closed(current,
                    action,
                    "invalid_focus_transition",
                    "Cannot focus modal without an active modal");
        return current;
      }

      current.focus = *action.focus;
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::BannerAdded:
      if (!action.banner.has_value()) {
        fail_closed(current,
                    action,
                    "missing_banner_payload",
                    "BannerAdded requires a banner payload");
        return current;
      }

      append_banner(current, *action.banner);
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::BannerCleared:
      current.banners.clear();
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::ModalShown:
      if (!action.modal.has_value()) {
        fail_closed(current,
                    action,
                    "missing_modal_payload",
                    "ModalShown requires a modal payload");
        return current;
      }

      current.modal = *action.modal;
      current.focus = TuiFocusState::Modal;
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::ModalHidden:
      current.modal = TuiModalState{};
      if (current.focus == TuiFocusState::Modal) {
        current.focus = TuiFocusState::Composer;
      }
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::SessionHydrated:
      if (!action.session.has_value()) {
        fail_closed(current,
                    action,
                    "missing_session_payload",
                    "SessionHydrated requires a session payload");
        return current;
      }

      current.session = *action.session;
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::StatusUpdated:
      if (!action.status.has_value()) {
        fail_closed(current,
                    action,
                    "missing_status_payload",
                    "StatusUpdated requires a status payload");
        return current;
      }

      current.status = *action.status;
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::RouteUpdated:
      if (!action.route.has_value()) {
        fail_closed(current,
                    action,
                    "missing_route_payload",
                    "RouteUpdated requires a route payload");
        return current;
      }

      current.route = *action.route;
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::EventAppended:
      if (!action.event.has_value()) {
        fail_closed(current,
                    action,
                    "missing_event_payload",
                    "EventAppended requires an event payload");
        return current;
      }

      if (action.event->status_delta.has_value()) {
        current.status = *action.event->status_delta;
      }
      if (action.event->banner_reason.has_value()) {
        TuiBanner banner;
        banner.level = TuiBannerLevel::Warning;
        banner.title = "Event notice";
        banner.message = *action.event->banner_reason;
        banner.reason_code = std::string{"event_banner"};
        append_banner(current, std::move(banner));
      }

      current.transcript.push_back(build_message_view(*action.event));
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::ComposerTextChanged:
      if (!action.composer_text.has_value()) {
        fail_closed(current,
                    action,
                    "missing_composer_text_payload",
                    "ComposerTextChanged requires a composer_text payload");
        return current;
      }

      current.composer.text = *action.composer_text;
      current.composer.dirty = !action.composer_text->empty();
      if (current.composer.mode != "submitting") {
        current.composer.mode = action.composer_text->empty() ? "ready" : "editing";
      }
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::ComposerModeChanged:
      if (!action.composer_mode.has_value()) {
        fail_closed(current,
                    action,
                    "missing_composer_mode_payload",
                    "ComposerModeChanged requires a composer_mode payload");
        return current;
      }

      current.composer.mode = *action.composer_mode;
      if (*action.composer_mode == "submitting") {
        current.composer.can_submit = false;
        current.composer.dirty = false;
        current.focus = TuiFocusState::Transcript;
      }
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::ComposerSubmitAvailabilityChanged:
      if (!action.composer_can_submit.has_value()) {
        fail_closed(current,
                    action,
                    "missing_composer_submit_payload",
                    "ComposerSubmitAvailabilityChanged requires a submit payload");
        return current;
      }

      current.composer.can_submit = *action.composer_can_submit;
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::ForegroundSessionResetApplied:
      current.session = data::TuiSessionView{};
      current.transcript.clear();
      current.status = data::TuiStatusProjection{};
      current.route = data::TuiModelRouteProjection{};
      current.composer = TuiComposerState{};
      current.focus = TuiFocusState::Composer;
      current.banners.clear();
      current.modal = TuiModalState{};
      clear_debug_reason_if_unspecified(current, action);
      return current;

    case TuiActionType::StatusQueryRequested:
    case TuiActionType::SessionQueryRequested:
    case TuiActionType::ForegroundSessionClearRequested:
    case TuiActionType::TurnSubmitRequested:
    case TuiActionType::ExitRequested:
      // Request actions are fulfilled by the app loop or data source.
      clear_debug_reason_if_unspecified(current, action);
      return current;
  }

  current.debug_reason = resolve_debug_reason(action, "unsupported_action");
  return current;
}

}  // namespace dasall::tui::model