#include "app/TuiApp.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "model/TuiReducer.h"

namespace dasall::tui::app {
namespace {

using dasall::tui::model::TuiAction;
using dasall::tui::model::TuiActionType;
using dasall::tui::model::TuiBanner;
using dasall::tui::model::TuiBannerLevel;
using dasall::tui::model::TuiModalKind;
using dasall::tui::model::TuiModalState;

void apply_reduced_action(model::TuiScreenModel& screen_model,
                          const model::TuiAction& action) {
  screen_model = model::reduce(std::move(screen_model), action);
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() &&
         std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  std::size_t end = text.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return std::string(text.substr(start, end - start));
}

[[nodiscard]] std::string session_modal_body(
    const data::TuiSessionView& session,
    const data::TuiModelRouteProjection& route) {
  std::ostringstream body;
  body << "session_id: " << session.session_id << '\n'
       << "profile_id: " << session.profile_id << '\n'
       << "startup_mode: " << session.startup_mode << '\n'
       << "readiness: " << session.daemon_readiness << '\n'
       << "route: " << route.current_provider_id << "/" << route.current_model_id;
  return body.str();
}

[[nodiscard]] std::string status_summary_message(
    const data::TuiStatusProjection& status) {
  std::ostringstream message;
  message << "stage=" << (status.stage.empty() ? "unknown" : trim_copy(status.stage))
          << ", tool="
          << (status.current_tool.empty() ? "none" : trim_copy(status.current_tool))
          << ", health="
          << (status.health_summary.empty() ? "unknown" : trim_copy(status.health_summary));
  return message.str();
}

[[nodiscard]] model::TuiAction make_banner_action(
    const model::TuiBannerLevel level,
    std::string title,
    std::string message,
    std::string reason_code,
    std::string debug_reason,
    const bool sticky = false) {
  TuiAction action;
  action.type = TuiActionType::BannerAdded;
  action.debug_reason = std::move(debug_reason);

  TuiBanner banner;
  banner.level = level;
  banner.title = std::move(title);
  banner.message = std::move(message);
  if (!reason_code.empty()) {
    banner.reason_code = std::move(reason_code);
  }
  banner.sticky = sticky;
  action.banner = std::move(banner);
  return action;
}

[[nodiscard]] model::TuiAction make_modal_action(
    model::TuiModalState modal,
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::ModalShown;
  action.debug_reason = std::move(debug_reason);
  action.modal = std::move(modal);
  return action;
}

[[nodiscard]] model::TuiAction make_route_action(
    data::TuiModelRouteProjection route,
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::RouteUpdated;
  action.debug_reason = std::move(debug_reason);
  action.route = std::move(route);
  return action;
}

[[nodiscard]] model::TuiAction make_event_action(
    data::TuiEventProjection event,
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::EventAppended;
  action.debug_reason = std::move(debug_reason);
  action.event = std::move(event);
  return action;
}

[[nodiscard]] model::TuiAction make_foreground_session_reset_action(
    std::string debug_reason) {
  TuiAction action;
  action.type = TuiActionType::ForegroundSessionResetApplied;
  action.debug_reason = std::move(debug_reason);
  return action;
}

[[nodiscard]] data::TuiStatusProjection make_baseline_status(
    std::string startup_mode) {
  data::TuiStatusProjection status;
  status.stage = "ready";
  status.current_tool = "";
  status.pending_interaction = "";
  status.budget_summary = "Budget 100% remaining";
  status.recovery_summary = "";
  status.health_summary = "healthy";
  status.safe_mode_summary = startup_mode == "line" ? "line" : "normal";
  return status;
}

[[nodiscard]] bool startup_issue_means_daemon_unavailable(
    const data::TuiDataSourceIssue& issue) {
  return issue.reason_code == "socket_missing" ||
         issue.reason_code == "daemon_unavailable";
}

[[nodiscard]] data::TuiEventProjection make_submit_receipt_event(
    const data::TuiTurnReceipt& receipt,
    std::string_view session_id) {
  data::TuiEventProjection event;
  event.event_kind = "turn_receipt";
  event.session_id = receipt.session_id.empty() ? std::string(session_id)
                                                : receipt.session_id;
  event.timestamp = receipt.submitted_at;
  event.turn_receipt = receipt;
  return event;
}

}  // namespace

TuiApp::TuiApp() = default;

TuiApp::TuiApp(terminal::TuiTerminalCapabilityProbe probe)
    : probe_(std::move(probe)) {}

int TuiApp::run(TuiAppOptions options) {
  initialize_components(options);

  if (data_source_ == nullptr) {
    if (output_stream_ != nullptr && !last_error_.empty()) {
      *output_stream_ << last_error_ << '\n';
    }
    shutdown_clean_ = false;
    return 1;
  }

  terminal_capabilities_ = options.probe_environment.has_value()
                               ? probe_.probe(*options.probe_environment)
                               : probe_.probe();
  startup_mode_ = probe_.select_startup_mode(terminal_capabilities_);
  terminal_width_ = options.terminal_width;
  terminal_height_ = options.terminal_height;

  if (startup_mode_ == terminal::TuiStartupMode::FailClosed) {
    last_error_ = probe_.format_startup_error(terminal_capabilities_);
    if (output_stream_ != nullptr && !last_error_.empty()) {
      *output_stream_ << last_error_ << '\n';
    }
    shutdown_clean_ = false;
    return 1;
  }

  if (!open_session()) {
    emit_startup_error();
    const int exit_code = shutdown();
    return exit_code == 0 ? 1 : exit_code;
  }

  if (!load_route_catalog()) {
    emit_startup_error();
    const int exit_code = shutdown();
    return exit_code == 0 ? 1 : exit_code;
  }

  sync_composer_state();
  for (std::size_t tick_index = 0; tick_index < options.bootstrap_tick_count; ++tick_index) {
    if (!tick()) {
      break;
    }
  }

  for (const auto& action : options.scripted_actions) {
    dispatch_action(action);
    if (exit_requested_) {
      break;
    }
  }

  if (!exit_requested_) {
    for (std::size_t tick_index = 0; tick_index < options.post_action_tick_count;
         ++tick_index) {
      if (!tick()) {
        break;
      }
    }
  }

  if (!exit_requested_ && options.selector_preview_mode.has_value()) {
    show_selector_preview(*options.selector_preview_mode);
  }

  render_current_screen(options.print_final_screen);
  return shutdown();
}

void TuiApp::dispatch_action(const model::TuiAction& action) {
  apply_reduced_action(screen_model_, action);

  switch (action.type) {
    case TuiActionType::StatusUpdated:
    case TuiActionType::EventAppended:
      sync_composer_busy_from_status();
      break;

    case TuiActionType::StatusQueryRequested:
      apply_reduced_action(screen_model_,
                           make_banner_action(model::TuiBannerLevel::Info,
                                              "Status summary",
                                              status_summary_message(screen_model_.status),
                                              "status_query",
                                              "status_query_requested"));
      break;

    case TuiActionType::SessionQueryRequested: {
      TuiModalState modal;
      modal.kind = TuiModalKind::Session;
      modal.title = "Session summary";
      modal.body = session_modal_body(screen_model_.session, screen_model_.route);
      modal.actions = {"Close"};
      modal.selected_action_index = 0;
      apply_reduced_action(screen_model_,
                           make_modal_action(std::move(modal),
                                             "session_query_requested"));
      break;
    }

    case TuiActionType::ForegroundSessionClearRequested:
      handle_foreground_session_clear();
      break;

    case TuiActionType::TurnSubmitRequested:
      dispatch_composer_submit();
      break;

    case TuiActionType::ExitRequested:
      exit_requested_ = true;
      shutdown_close_reason_ = "/exit";
      break;

    case TuiActionType::RouteUpdated:
    case TuiActionType::Noop:
    case TuiActionType::FocusChanged:
    case TuiActionType::BannerAdded:
    case TuiActionType::BannerCleared:
    case TuiActionType::ModalShown:
    case TuiActionType::ModalHidden:
    case TuiActionType::SessionHydrated:
    case TuiActionType::ComposerTextChanged:
    case TuiActionType::ComposerModeChanged:
    case TuiActionType::ComposerSubmitAvailabilityChanged:
    case TuiActionType::ForegroundSessionResetApplied:
      break;
  }

  render_current_screen(false);
}

bool TuiApp::tick() {
  if (!session_open_ || session_id_.empty() || data_source_ == nullptr) {
    return false;
  }

  const data::TuiPollEventsRequest request{
      .session_id = session_id_,
      .event_cursor = last_event_cursor_,
      .request_id = next_request_id("poll_events"),
      .trace_id = next_trace_id("poll_events"),
  };
  data::TuiPollEventsResult result = data_source_->poll_events(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = result.issue->message;
      append_issue_banner(*result.issue, "Event poll failed");
      render_current_screen(false);
    }
    return false;
  }

  last_event_cursor_ = result.next_cursor;
  if (result.events.empty()) {
    return false;
  }

  for (const auto& event : result.events) {
    TuiAction action;
    action.type = TuiActionType::EventAppended;
    action.debug_reason = "tick:" + event.event_kind;
    action.event = event;
    dispatch_action(action);
  }

  return true;
}

int TuiApp::shutdown() {
  if (!session_open_ || session_id_.empty() || data_source_ == nullptr) {
    shutdown_clean_ = last_error_.empty();
    return last_error_.empty() ? 0 : 1;
  }

  const data::TuiCloseSessionRequest request{
      .session_id = session_id_,
      .close_reason = shutdown_close_reason_,
      .request_id = next_request_id("close_session"),
      .trace_id = next_trace_id("close_session"),
  };
  data::TuiCloseSessionResult result = data_source_->close_session(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = result.issue->message;
      append_issue_banner(*result.issue, "Session close failed");
      render_current_screen(false);
    }
    shutdown_clean_ = false;
    return 1;
  }

  session_open_ = false;
  shutdown_clean_ = true;
  return 0;
}

void TuiApp::clear_composer_draft_preserving_history() {
  static_cast<void>(composer_.set_busy(false));
  const view::TuiComposerKeyEvent event{
      .key = view::TuiComposerKey::TextChanged,
      .text = std::string{},
  };
  static_cast<void>(composer_.handle_key(event));
}

void TuiApp::restore_composer_draft(std::string text,
                                    const std::size_t cursor_offset) {
  static_cast<void>(composer_.set_busy(false));
  const view::TuiComposerKeyEvent event{
      .key = view::TuiComposerKey::TextChanged,
      .text = std::move(text),
      .cursor_offset = cursor_offset,
  };
  static_cast<void>(composer_.handle_key(event));
  sync_composer_state();
}

void TuiApp::dispatch_composer_submit() {
  if (!session_open_ || session_id_.empty() || data_source_ == nullptr) {
    apply_reduced_action(screen_model_,
                         make_banner_action(model::TuiBannerLevel::Error,
                                            "Turn submit unavailable",
                                            "No active session is attached to the formal submit path.",
                                            "submit_unavailable",
                                            "turn_submit_unavailable",
                                            true));
    return;
  }

  const std::string previous_draft = composer_.state().text;
  const std::size_t previous_cursor_offset = composer_.state().cursor_offset;
  const view::TuiComposerUpdate update = composer_.handle_key(
      view::TuiComposerKeyEvent{.key = view::TuiComposerKey::Enter, .text = {}});
  sync_composer_state();

  if (update.action.type != view::TuiComposerActionType::SubmitRequested) {
    return;
  }

  const data::TuiSubmitTurnRequest request{
      .session_id = session_id_,
      .user_input = update.action.text,
      .next_preference = screen_model_.route.next_preference,
      .request_id = next_request_id("submit_turn"),
      .trace_id = next_trace_id("submit_turn"),
  };
  data::TuiSubmitTurnResult result = data_source_->submit_turn(request);
  if (!result.ok()) {
    restore_composer_draft(previous_draft, previous_cursor_offset);
    if (result.issue.has_value()) {
      last_error_ = result.issue->message;
      append_issue_banner(*result.issue,
                          result.issue->reason_code == "validation_failed"
                              ? "Turn submit rejected"
                              : "Turn submit failed");
    }
    return;
  }

  last_error_.clear();
  apply_reduced_action(screen_model_,
                       make_event_action(
                           make_submit_receipt_event(*result.receipt, session_id_),
                           "submit_turn_receipt"));
  apply_reduced_action(screen_model_,
                       make_banner_action(
                           model::TuiBannerLevel::Info,
                           "Turn submitted",
                           result.receipt->summary_text.empty()
                               ? result.receipt->disposition
                               : result.receipt->summary_text,
                           result.receipt->reason_code.value_or(std::string{}),
                           "turn_submit_requested"));
}

void TuiApp::handle_foreground_session_clear() {
  std::optional<data::TuiDataSourceIssue> close_issue;
  if (session_open_ && !session_id_.empty() && data_source_ != nullptr) {
    const data::TuiCloseSessionRequest close_request{
        .session_id = session_id_,
        .close_reason = "/clear",
        .request_id = next_request_id("close_session"),
        .trace_id = next_trace_id("close_session"),
    };
    const data::TuiCloseSessionResult close_result =
        data_source_->close_session(close_request);
    if (!close_result.ok() && close_result.issue.has_value()) {
      close_issue = close_result.issue;
    }
  }

  session_open_ = false;
  session_id_.clear();
  last_event_cursor_.reset();
  last_error_.clear();

  clear_composer_draft_preserving_history();
  apply_reduced_action(screen_model_,
                       make_foreground_session_reset_action(
                           "foreground_session_clear_reset"));
  sync_composer_state();

  const bool session_reopened = open_session();
  const bool route_reloaded = session_reopened && load_route_catalog();

  if (close_issue.has_value()) {
    apply_reduced_action(screen_model_,
                         make_banner_action(model::TuiBannerLevel::Warning,
                                            "Previous session close not confirmed",
                                            close_issue->message,
                                            close_issue->reason_code,
                                            "foreground_session_clear_close_issue",
                                            true));
  }

  if (session_reopened && route_reloaded) {
    sync_composer_busy_from_status();
  }
}

const model::TuiScreenModel& TuiApp::screen_model() const noexcept {
  return screen_model_;
}

const std::vector<std::string>& TuiApp::rendered_frames() const noexcept {
  return rendered_frames_;
}

const std::string& TuiApp::last_rendered_screen() const noexcept {
  return last_rendered_screen_;
}

const terminal::TuiTerminalCapabilities& TuiApp::terminal_capabilities() const noexcept {
  return terminal_capabilities_;
}

terminal::TuiStartupMode TuiApp::startup_mode() const noexcept {
  return startup_mode_;
}

bool TuiApp::session_open() const noexcept {
  return session_open_;
}

bool TuiApp::shutdown_clean() const noexcept {
  return shutdown_clean_;
}

std::string_view TuiApp::last_error() const noexcept {
  return last_error_;
}

void TuiApp::initialize_components(TuiAppOptions& options) {
  const bool has_initial_draft = options.initial_draft.has_value() &&
                                 !options.initial_draft->empty();

  output_stream_ = options.output_stream;
  scenario_id_ = options.scenario_id.empty() ? "planning_tools" : options.scenario_id;
  profile_id_ = options.profile_id;
  composer_ = view::TuiComposer(model::TuiComposerState{
      .text = options.initial_draft.value_or(std::string{}),
      .mode = has_initial_draft ? "editing" : "ready",
      .history_query = std::nullopt,
      .can_submit = true,
      .dirty = has_initial_draft,
        .cursor_visible = true,
        .activity_indicator = {},
  });
  selector_ = view::TuiModelSelector{};
  screen_model_ = model::TuiScreenModel{};
  screen_model_.composer = composer_.state();
  screen_model_.focus = model::TuiFocusState::Composer;
  screen_model_.debug_reason = "tui_app_initialized";
  terminal_capabilities_ = {};
  startup_mode_ = terminal::TuiStartupMode::FailClosed;
  last_event_cursor_.reset();
  rendered_frames_.clear();
  last_rendered_screen_.clear();
  last_error_.clear();
  data_source_ = std::move(options.data_source_override);
  if (data_source_ == nullptr) {
    last_error_ = "no TUI data source configured for this entrypoint";
  }
  session_id_.clear();
  request_sequence_ = 0;
  session_open_ = false;
  shutdown_clean_ = false;
  shutdown_close_reason_ = "prototype_round_complete";
  exit_requested_ = false;
}

bool TuiApp::open_session() {
  const data::TuiOpenSessionRequest request{
      .profile_id = profile_id_,
      .startup_mode_hint = startup_mode_to_string(startup_mode_),
      .request_id = next_request_id("open_session"),
      .trace_id = next_trace_id("open_session"),
  };
  data::TuiOpenSessionResult result = data_source_->open_session(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = format_startup_issue_message(*result.issue);
      append_issue_banner(*result.issue, "Session open failed");
    }
    return false;
  }

  session_id_ = result.session->session_id;
  session_open_ = true;

  TuiAction session_action;
  session_action.type = TuiActionType::SessionHydrated;
  session_action.debug_reason = "open_session";
  session_action.session = result.session;
  apply_reduced_action(screen_model_, session_action);

  TuiAction status_action;
  status_action.type = TuiActionType::StatusUpdated;
  status_action.debug_reason = "baseline_status";
  status_action.status = make_baseline_status(startup_mode_to_string(startup_mode_));
  apply_reduced_action(screen_model_, status_action);
  sync_composer_busy_from_status();
  return true;
}

bool TuiApp::load_route_catalog() {
  const data::TuiRouteCatalogRequest request{
      .session_id = session_id_.empty() ? std::nullopt : std::optional<std::string>{session_id_},
      .profile_id = profile_id_,
      .selector_mode = std::nullopt,
      .request_id = next_request_id("route_catalog"),
      .trace_id = next_trace_id("route_catalog"),
  };
  data::TuiRouteCatalogResult result = data_source_->route_catalog(request);
  if (!result.ok()) {
    if (result.issue.has_value()) {
      last_error_ = format_startup_issue_message(*result.issue);
      append_issue_banner(*result.issue, "Route catalog failed");
    }
    return false;
  }

  selector_.set_route_catalog(*result.route_catalog);
  apply_reduced_action(screen_model_,
                       make_route_action(result.route_catalog->current_route,
                                         "route_catalog_loaded"));
  return true;
}

void TuiApp::sync_composer_state() {
  TuiAction text_action;
  text_action.type = TuiActionType::ComposerTextChanged;
  text_action.debug_reason = "composer_state_sync:text";
  text_action.composer_text = composer_.state().text;
  apply_reduced_action(screen_model_, text_action);

  TuiAction mode_action;
  mode_action.type = TuiActionType::ComposerModeChanged;
  mode_action.debug_reason = "composer_state_sync:mode";
  mode_action.composer_mode = composer_.state().mode;
  apply_reduced_action(screen_model_, mode_action);

  TuiAction submit_action;
  submit_action.type = TuiActionType::ComposerSubmitAvailabilityChanged;
  submit_action.debug_reason = "composer_state_sync:submit";
  submit_action.composer_can_submit = composer_.state().can_submit;
  apply_reduced_action(screen_model_, submit_action);
}

void TuiApp::sync_composer_busy_from_status() {
  static_cast<void>(composer_.set_busy(status_requires_busy_composer(screen_model_.status)));
  sync_composer_state();
}

void TuiApp::show_selector_preview(const data::TuiRoutePreferenceMode mode) {
  const auto options = selector_.open_selector(mode);
  const data::NextTurnPreference next_preference = selector_.apply_preference();

  data::TuiModelRouteProjection route = screen_model_.route;
  route.next_preference = next_preference;
  apply_reduced_action(screen_model_,
                       make_route_action(std::move(route),
                                         "selector_preview_applied"));

  TuiModalState modal;
  modal.kind = TuiModalKind::Selector;
  modal.title = "Next turn preference";
  modal.body = selector_modal_body(options);
  modal.actions = {"Apply", "Cancel"};
  modal.selected_action_index = 0;
  apply_reduced_action(screen_model_,
                       make_modal_action(std::move(modal),
                                         "selector_preview_modal"));
}

void TuiApp::render_current_screen(const bool flush_to_output) {
  last_rendered_screen_ = renderer_.render_to_screen(
      screen_model_,
      effective_terminal_width(),
      effective_terminal_height());
  rendered_frames_.push_back(last_rendered_screen_);

  if (flush_to_output && output_stream_ != nullptr) {
    *output_stream_ << last_rendered_screen_ << '\n';
  }
}

void TuiApp::emit_startup_error() const {
  if (output_stream_ != nullptr && !last_error_.empty()) {
    *output_stream_ << last_error_ << '\n';
  }
}

std::string TuiApp::selector_modal_body(
    const std::vector<view::TuiModelSelectorOption>& options) const {
  if (options.empty()) {
    return "No selector options are required for auto mode.";
  }

  std::ostringstream body;
  body << "Draft: " << screen_model_.route.next_preference.user_visible_summary << '\n';
  for (const auto& option : options) {
    body << (option.selected ? "* " : "- ") << option.display_label;
    if (!option.selectable) {
      body << " [disabled: "
           << selector_.render_disabled_reason(option.disabled_reasons)
           << ']';
    }
    body << '\n';
  }
  return body.str();
}

std::size_t TuiApp::effective_terminal_width() const noexcept {
  if (terminal_width_ > 0) {
    return terminal_width_;
  }
  if (terminal_capabilities_.columns > 0) {
    return static_cast<std::size_t>(terminal_capabilities_.columns);
  }

  switch (startup_mode_) {
    case terminal::TuiStartupMode::FullScreen:
      return 120;
    case terminal::TuiStartupMode::Narrow:
      return 80;
    case terminal::TuiStartupMode::Line:
    case terminal::TuiStartupMode::FailClosed:
      return 40;
  }

  return 40;
}

std::size_t TuiApp::effective_terminal_height() const noexcept {
  if (terminal_height_ > 0) {
    return terminal_height_;
  }
  if (terminal_capabilities_.rows > 0) {
    return static_cast<std::size_t>(terminal_capabilities_.rows);
  }

  switch (startup_mode_) {
    case terminal::TuiStartupMode::FullScreen:
      return 36;
    case terminal::TuiStartupMode::Narrow:
      return 24;
    case terminal::TuiStartupMode::Line:
    case terminal::TuiStartupMode::FailClosed:
      return 12;
  }

  return 12;
}

std::string TuiApp::format_startup_issue_message(
    const data::TuiDataSourceIssue& issue) {
  const std::string detail = trim_copy(issue.message);

  if (issue.reason_code == "permission_denied") {
    std::string message =
        "TUI startup blocked: permission denied for the current root/sudo-only operator path.";
    if (!detail.empty()) {
      message += ' ';
      message += detail;
    }
    return message;
  }

  if (startup_issue_means_daemon_unavailable(issue)) {
    std::string message =
        "TUI startup blocked: daemon unavailable for the local control-plane path.";
    if (!detail.empty()) {
      message += ' ';
      message += detail;
    }
    return message;
  }

  if (issue.reason_code == "profile_missing") {
    std::string message =
        "TUI startup blocked: requested profile is missing or incomplete.";
    if (!detail.empty()) {
      message += ' ';
      message += detail;
    }
    return message;
  }

  if (!detail.empty()) {
    return "TUI startup blocked: " + detail;
  }

  if (issue.has_reason()) {
    return "TUI startup blocked: " + issue.reason_domain + '/' + issue.reason_code;
  }

  return "TUI startup blocked: startup requirements are not satisfied.";
}

std::string TuiApp::startup_mode_to_string(
    const terminal::TuiStartupMode startup_mode) {
  switch (startup_mode) {
    case terminal::TuiStartupMode::FullScreen:
      return "full";
    case terminal::TuiStartupMode::Narrow:
      return "narrow";
    case terminal::TuiStartupMode::Line:
      return "line";
    case terminal::TuiStartupMode::FailClosed:
      return "fail_closed";
  }

  return "fail_closed";
}

bool TuiApp::status_requires_busy_composer(const data::TuiStatusProjection& status) {
  return !trim_copy(status.pending_interaction).empty() ||
         !trim_copy(status.current_tool).empty() ||
         trim_copy(status.stage) == "tool_calling" ||
         trim_copy(status.stage) == "waiting_interaction";
}

std::string TuiApp::next_request_id(std::string_view prefix) {
  ++request_sequence_;
  return std::string(prefix) + "-" + scenario_id_ + "-" +
         std::to_string(request_sequence_);
}

std::string TuiApp::next_trace_id(std::string_view prefix) {
  return "trace-" + next_request_id(prefix);
}

void TuiApp::append_issue_banner(const data::TuiDataSourceIssue& issue, std::string title) {
  apply_reduced_action(screen_model_,
                       make_banner_action(model::TuiBannerLevel::Error,
                                          std::move(title),
                                          issue.message,
                                          issue.reason_code,
                                          "data_source_issue:" + issue.reason_code,
                                          true));
}

}  // namespace dasall::tui::app