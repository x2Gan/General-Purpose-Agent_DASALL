#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/TuiApp.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::app::TuiApp;
using dasall::tui::app::TuiAppOptions;
using dasall::tui::data::ITuiDataSource;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiCloseSessionRequest;
using dasall::tui::data::TuiCloseSessionResult;
using dasall::tui::data::TuiDataSourceIssue;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiModelRouteProjection;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::data::TuiOpenSessionResult;
using dasall::tui::data::TuiPollEventsRequest;
using dasall::tui::data::TuiPollEventsResult;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogRequest;
using dasall::tui::data::TuiRouteCatalogResult;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSessionView;
using dasall::tui::data::TuiSubmitTurnRequest;
using dasall::tui::data::TuiSubmitTurnResult;
using dasall::tui::data::TuiTurnReceipt;
using dasall::tui::model::TuiAction;
using dasall::tui::model::TuiActionType;
using dasall::tui::terminal::TuiTerminalProbeEnvironment;

class RecordingDataSource final : public ITuiDataSource {
 public:
  TuiOpenSessionResult open_session(const TuiOpenSessionRequest&) override {
    return TuiOpenSessionResult{.session = session, .issue = std::nullopt};
  }

  TuiSubmitTurnResult submit_turn(const TuiSubmitTurnRequest& request) override {
    submit_requests.push_back(request);
    if (submit_issue.has_value()) {
      return TuiSubmitTurnResult{.receipt = std::nullopt, .issue = submit_issue};
    }
    return TuiSubmitTurnResult{.receipt = submit_receipt, .issue = std::nullopt};
  }

  TuiPollEventsResult poll_events(const TuiPollEventsRequest&) override {
    if (poll_events_delivered) {
      return TuiPollEventsResult{.events = {}, .next_cursor = next_cursor, .issue = std::nullopt};
    }
    poll_events_delivered = true;
    return TuiPollEventsResult{.events = poll_events_batch, .next_cursor = next_cursor, .issue = std::nullopt};
  }

  TuiRouteCatalogResult route_catalog(const TuiRouteCatalogRequest&) override {
    return TuiRouteCatalogResult{.route_catalog = route_catalog_view, .issue = std::nullopt};
  }

  TuiCloseSessionResult close_session(const TuiCloseSessionRequest&) override {
    return TuiCloseSessionResult{.closed = true, .issue = std::nullopt};
  }

  TuiSessionView session{
      .session_id = "session-submit-040",
      .profile_id = "desktop_full",
      .daemon_readiness = "accepted",
      .startup_mode = "full",
      .started_at = "2026-05-25T12:40:00Z",
  };
  TuiRouteCatalogView route_catalog_view;
  std::optional<TuiTurnReceipt> submit_receipt;
  std::optional<TuiDataSourceIssue> submit_issue;
  std::vector<TuiSubmitTurnRequest> submit_requests;
  std::vector<TuiEventProjection> poll_events_batch;
  std::optional<std::string> next_cursor;
  bool poll_events_delivered = false;
};

[[nodiscard]] TuiTerminalProbeEnvironment make_full_screen_environment() {
  TuiTerminalProbeEnvironment environment;
  environment.stdin_is_tty = true;
  environment.stdout_is_tty = true;
  environment.stderr_is_tty = true;
  environment.term = "xterm-256color";
  environment.locale = "en_US.UTF-8";
  environment.columns = 120;
  environment.rows = 36;
  environment.utf8_enabled = true;
  environment.bracketed_paste_supported = true;
  environment.resize_events_supported = true;
  environment.external_editor_available = true;
  return environment;
}

[[nodiscard]] TuiRouteCatalogView make_route_catalog() {
  TuiRouteCatalogView route_catalog;
  route_catalog.current_route = TuiModelRouteProjection{
      .current_provider_id = "deepseek-prod",
      .current_model_id = "deepseek-reasoner",
      .current_depth_tier = "deep",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .disabled_reasons = {},
      .next_preference = NextTurnPreference{
          .mode = TuiRoutePreferenceMode::PreferDepth,
          .preferred_depth_tier = std::optional<std::string>("deep"),
          .pinned_provider_id = std::nullopt,
          .pinned_model_id = std::nullopt,
          .user_visible_summary = "prefer depth: deep",
          .source = "tui_model_selector",
          .applies_to_next_turn_only = true,
      },
  };

  route_catalog.candidate_routes.push_back(TuiRouteCatalogEntry{
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-reasoner",
      .depth_tier = "deep",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .selectable = true,
      .disabled_reasons = {},
  });
  return route_catalog;
}

[[nodiscard]] TuiTurnReceipt make_success_receipt() {
  return TuiTurnReceipt{
      .request_id = "submit-receipt-040",
      .trace_id = "trace-submit-receipt-040",
      .session_id = "session-submit-040",
      .disposition = "accepted_async",
      .receipt_ref = "receipt-ref-040",
      .submitted_at = "2026-05-25T12:40:01Z",
      .summary_text = "submit turn accepted for daemon execution",
      .response_text = std::nullopt,
      .reason_code = std::nullopt,
  };
}

[[nodiscard]] TuiTurnReceipt make_completed_receipt() {
  TuiTurnReceipt receipt{
      .request_id = "submit-receipt-completed-040",
      .trace_id = "trace-submit-receipt-completed-040",
      .session_id = "session-submit-040",
      .disposition = "completed",
      .receipt_ref = "receipt-ref-completed-040",
      .submitted_at = "2026-05-25T12:40:02Z",
      .summary_text = "completed by daemon-backed execution",
      .response_text = std::string("llm.origin=deepseek-prod/deepseek-reasoner final answer"),
      .reason_code = std::nullopt,
  };
  return receipt;
}

[[nodiscard]] TuiEventProjection make_completed_event() {
  TuiEventProjection event;
  event.event_cursor = "cursor-completed-040";
  event.event_kind = "turn.receipt";
  event.session_id = "session-submit-040";
  event.timestamp = "2026-05-25T12:40:03Z";
  event.turn_receipt = make_completed_receipt();
  event.status_delta = dasall::tui::data::TuiStatusProjection{
      .stage = "completed",
      .current_tool = "access.submit",
      .pending_interaction = "none",
      .budget_summary = "budget ok",
      .recovery_summary = "stable",
      .health_summary = "healthy",
      .safe_mode_summary = "completed by daemon-backed execution",
  };
  return event;
}

[[nodiscard]] TuiAction make_submit_action() {
  TuiAction action;
  action.type = TuiActionType::TurnSubmitRequested;
  action.debug_reason = "scripted_turn_submit";
  return action;
}

void tui_app_submit_turn_assembles_request_and_projects_receipt() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  data_source->submit_receipt = make_success_receipt();
  RecordingDataSource* const probe = data_source.get();

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_submit";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.initial_draft = std::string("Hello from formal submit path");
  options.scripted_actions.push_back(make_submit_action());
  options.data_source_override = std::move(data_source);

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "successful submit-turn integration should keep the formal app run green");
  assert_equal(1, static_cast<int>(probe->submit_requests.size()),
               "turn submit integration should call the data source exactly once");
  assert_equal(std::string("session-submit-040"),
               probe->submit_requests.front().session_id,
               "submit request should carry the foreground session id");
  assert_equal(std::string("Hello from formal submit path"),
               probe->submit_requests.front().user_input,
               "submit request should preserve the composer draft text");
  assert_true(
      probe->submit_requests.front().request_id.find("submit_turn-formal_submit-") == 0,
      "submit request id should be generated through the app-scoped submit_turn prefix");
  assert_true(
      probe->submit_requests.front().trace_id.find("trace-submit_turn-formal_submit-") == 0,
      "submit trace id should be generated through the app-scoped submit_turn prefix");
  assert_true(probe->submit_requests.front().next_preference.mode ==
                  TuiRoutePreferenceMode::PreferDepth,
              "submit request should preserve the current route next-turn preference mode");
  assert_equal(std::string("deep"),
               probe->submit_requests.front().next_preference.preferred_depth_tier.value_or(
                   std::string()),
               "submit request should preserve the current route preferred depth tier");

  assert_equal(1, static_cast<int>(app.screen_model().transcript.size()),
               "successful submit should append the submitted user message into the transcript");
  assert_equal(std::string("user"),
               app.screen_model().transcript.back().role,
               "successful submit transcript entry should be attributed to the user");
  assert_equal(std::string("Hello from formal submit path"),
               app.screen_model().transcript.back().content,
               "successful submit transcript entry should surface the composer draft text");
  assert_equal(std::string("submitting"),
               app.screen_model().composer.mode,
               "successful submit should keep the composer in submitting mode until later polling updates arrive");
  assert_true(app.screen_model().composer.activity_indicator.find("processing") !=
                  std::string::npos,
              "successful submit should expose a processing spinner while the daemon turn is pending");
  assert_true(app.last_rendered_screen().find("processing") != std::string::npos,
              "successful submit should render the processing spinner before the next poll completes");
  assert_equal(std::string(),
               app.screen_model().composer.text,
               "successful submit should clear the composer draft after dispatch");
  assert_equal(std::string("Turn submitted"),
               app.screen_model().banners.back().title,
               "successful submit should surface an informational submit banner");
}

void tui_app_submit_turn_restores_draft_and_surfaces_validation_rejection() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  data_source->submit_receipt.reset();
  data_source->submit_issue = TuiDataSourceIssue{
      .reason_domain = "request",
      .reason_code = "validation_failed",
      .message = "user input rejected by submit validation",
      .retryable = false,
      .error_ref = std::nullopt,
      .metadata = {},
  };
  RecordingDataSource* const probe = data_source.get();

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_submit_validation";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.initial_draft = std::string("retry this prompt");
  options.scripted_actions.push_back(make_submit_action());
  options.data_source_override = std::move(data_source);

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "validation rejection should stay user-visible without crashing the app loop");
  assert_equal(1, static_cast<int>(probe->submit_requests.size()),
               "validation rejection should still exercise the submit_turn data source path");
  assert_equal(std::string("retry this prompt"),
               app.screen_model().composer.text,
               "validation rejection should restore the previous draft so the user can retry");
  assert_equal(std::string("editing"),
               app.screen_model().composer.mode,
               "validation rejection should return the composer to editable mode");
  assert_true(app.screen_model().composer.can_submit,
              "validation rejection should restore submit availability for the composer");
  assert_true(app.screen_model().transcript.empty(),
              "validation rejection should not append a fake receipt into the transcript");
  assert_equal(std::string("validation_failed"),
               app.screen_model().banners.back().reason_code.value_or(std::string()),
               "validation rejection should surface the stable validation_failed reason code");
  assert_equal(std::string("Turn submit rejected"),
               app.screen_model().banners.back().title,
               "validation rejection should surface a dedicated submit rejection banner title");
  assert_true(app.last_error().find("validation") != std::string_view::npos,
              "validation rejection should remain visible through the app-level last_error surface");
}

void tui_app_submit_turn_completed_event_releases_composer_and_projects_response_text() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  data_source->submit_receipt = make_completed_receipt();
  data_source->poll_events_batch.push_back(make_completed_event());
  data_source->next_cursor = std::string("cursor-completed-040");

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_submit_completed";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.initial_draft = std::string("Who are you?");
  options.scripted_actions.push_back(make_submit_action());
  options.post_action_tick_count = 1;
  options.data_source_override = std::move(data_source);

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "completed daemon-backed submit should keep the app run green");
  assert_equal(2,
               static_cast<int>(app.screen_model().transcript.size()),
               "completed daemon-backed submit should keep one user row and one assistant row");
  assert_equal(std::string("user"),
               app.screen_model().transcript.front().role,
               "completed daemon-backed submit should preserve the submitted user row");
  assert_equal(std::string("Who are you?"),
               app.screen_model().transcript.front().content,
               "completed daemon-backed submit should show the original user text");
  assert_equal(std::string("llm.origin=deepseek-prod/deepseek-reasoner final answer"),
               app.screen_model().transcript.back().content,
               "completed daemon-backed submit should project response_text instead of the generic receipt summary");
  assert_equal(std::string("assistant"),
               app.screen_model().transcript.back().role,
               "completed daemon-backed submit should attribute response_text to the assistant");
  assert_equal(std::string("ready"),
               app.screen_model().composer.mode,
               "completed daemon-backed submit should release the composer after terminal status arrives");
  assert_true(app.screen_model().composer.can_submit,
              "completed daemon-backed submit should restore submit availability");
}

}  // namespace

int main() {
  try {
    tui_app_submit_turn_assembles_request_and_projects_receipt();
    tui_app_submit_turn_restores_draft_and_surfaces_validation_rejection();
    tui_app_submit_turn_completed_event_releases_composer_and_projects_response_text();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}