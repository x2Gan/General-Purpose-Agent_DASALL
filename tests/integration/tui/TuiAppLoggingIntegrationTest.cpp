#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "app/TuiApp.h"
#include "logging/RedactionFilter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::logging::LogEvent;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LogLevel;
using dasall::infra::logging::LogWriteResult;
using dasall::infra::logging::RedactionFilter;
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

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  LogWriteResult log(const LogEvent& event) override {
    events.push_back(event);
    if (fail_log) {
      return LogWriteResult::failure(dasall::contracts::ResultCode::ProviderTimeout,
                                     "forced TUI logger write failure",
                                     "logging.sink.io",
                                     "TuiAppLoggingIntegrationTest");
    }
    return LogWriteResult::success();
  }

  LogWriteResult flush(const LogFlushDeadline&) override {
    ++flush_count;
    if (fail_flush) {
      return LogWriteResult::failure(dasall::contracts::ResultCode::ProviderTimeout,
                                     "forced TUI logger flush failure",
                                     "logging.flush",
                                     "TuiAppLoggingIntegrationTest");
    }
    return LogWriteResult::success();
  }

  void set_level(LogLevel level) override { last_level = level; }

  std::vector<LogEvent> events;
  std::size_t flush_count = 0;
  LogLevel last_level = LogLevel::Info;
  bool fail_log = false;
  bool fail_flush = false;
};

class RecordingDataSource final : public ITuiDataSource {
 public:
  TuiOpenSessionResult open_session(const TuiOpenSessionRequest& request) override {
    open_requests.push_back(request);
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
    return TuiPollEventsResult{.events = poll_events_batch,
                               .next_cursor = next_cursor,
                               .issue = std::nullopt};
  }

  TuiRouteCatalogResult route_catalog(const TuiRouteCatalogRequest&) override {
    return TuiRouteCatalogResult{.route_catalog = route_catalog_view,
                                 .issue = std::nullopt};
  }

  TuiCloseSessionResult close_session(const TuiCloseSessionRequest& request) override {
    close_requests.push_back(request);
    if (close_issue.has_value()) {
      return TuiCloseSessionResult{.closed = false, .issue = close_issue};
    }
    return TuiCloseSessionResult{.closed = true, .issue = std::nullopt};
  }

  TuiSessionView session{
      .session_id = "session-tui-logging",
      .profile_id = "desktop_full",
      .daemon_readiness = "accepted",
      .startup_mode = "full",
      .started_at = "2026-05-29T09:00:00Z",
  };
  TuiRouteCatalogView route_catalog_view;
  std::optional<TuiTurnReceipt> submit_receipt;
  std::optional<TuiDataSourceIssue> submit_issue;
  std::optional<TuiDataSourceIssue> close_issue;
  std::vector<TuiOpenSessionRequest> open_requests;
  std::vector<TuiSubmitTurnRequest> submit_requests;
  std::vector<TuiCloseSessionRequest> close_requests;
  std::vector<TuiEventProjection> poll_events_batch;
  std::optional<std::string> next_cursor;
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
      .request_id = "receipt-tui-logging",
      .trace_id = "trace-receipt-tui-logging",
      .session_id = "session-tui-logging",
      .disposition = "accepted_async",
      .receipt_ref = "receipt-ref-tui-logging",
      .submitted_at = "2026-05-29T09:00:01Z",
      .summary_text = "turn accepted",
      .response_text = std::nullopt,
      .reason_code = std::nullopt,
  };
}

[[nodiscard]] TuiAction make_submit_action() {
  TuiAction action;
  action.type = TuiActionType::TurnSubmitRequested;
  action.debug_reason = "scripted_logging_submit";
  return action;
}

[[nodiscard]] const LogEvent* find_event(const std::vector<LogEvent>& events,
                                         std::string_view event_name,
                                         std::string_view outcome = {}) {
  for (const auto& event : events) {
    const auto event_name_it = event.attrs.find("event_name");
    if (event_name_it == event.attrs.end() || event_name_it->second != event_name) {
      continue;
    }
    if (outcome.empty()) {
      return &event;
    }
    const auto outcome_it = event.attrs.find("outcome");
    if (outcome_it != event.attrs.end() && outcome_it->second == outcome) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] const LogEvent* find_event_with_attr(
    const std::vector<LogEvent>& events,
    std::string_view event_name,
    std::string_view attr_key,
    std::string_view attr_value) {
  for (const auto& event : events) {
    const auto event_name_it = event.attrs.find("event_name");
    if (event_name_it == event.attrs.end() || event_name_it->second != event_name) {
      continue;
    }
    const auto attr_it = event.attrs.find(std::string(attr_key));
    if (attr_it != event.attrs.end() && attr_it->second == attr_value) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] bool event_contains_text(const LogEvent& event,
                                       const std::string& forbidden_text) {
  if (event.message.find(forbidden_text) != std::string::npos) {
    return true;
  }
  return std::any_of(event.attrs.begin(),
                     event.attrs.end(),
                     [&forbidden_text](const auto& entry) {
                       return entry.first.find(forbidden_text) != std::string::npos ||
                              entry.second.find(forbidden_text) != std::string::npos;
                     });
}

void tui_app_records_low_sensitive_structured_events() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  data_source->submit_receipt = make_success_receipt();
  RecordingDataSource* const data_source_probe = data_source.get();
  auto logger = std::make_shared<RecordingLogger>();

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_logging";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.initial_draft = std::string("user secret prompt token=abc123");
  options.scripted_actions.push_back(make_submit_action());
  options.data_source_override = std::move(data_source);
  options.logger = logger;

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "logging integration should not change successful TUI execution");
  assert_true(find_event(logger->events, "tui.startup", "accepted") != nullptr,
              "TUI logging should record startup acceptance");
  assert_true(find_event(logger->events, "tui.session.open", "success") != nullptr,
              "TUI logging should record session open success");
  assert_true(find_event(logger->events, "tui.route_catalog", "success") != nullptr,
              "TUI logging should record route catalog success");
  const LogEvent* submit_event = find_event(logger->events, "tui.turn.submit", "success");
  assert_true(submit_event != nullptr,
              "TUI logging should record turn submit success");
  assert_true(find_event(logger->events, "tui.render.frame", "success") != nullptr,
              "TUI logging should record render frames at debug level");
  assert_true(find_event(logger->events, "tui.session.close", "success") != nullptr,
              "TUI logging should record session close success");
  assert_true(logger->flush_count > 0U,
              "TUI logging should flush on shutdown so async file sinks can persist final events");
  assert_equal(1,
               static_cast<int>(data_source_probe->submit_requests.size()),
               "logging integration should still execute one submit request");
  assert_equal(std::string("session-tui-logging"),
               submit_event->attrs.at("session_id"),
               "submit log event should carry session correlation");
  assert_true(submit_event->attrs.at("request_id").find("submit_turn-formal_logging-") == 0,
              "submit log event should carry generated request id");
  assert_true(submit_event->attrs.at("trace_id").find("trace-submit_turn-formal_logging-") == 0,
              "submit log event should carry generated trace id");
  for (const auto& event : logger->events) {
    assert_true(!event_contains_text(event, "user secret prompt token=abc123"),
                "TUI logs must not persist raw composer user input");
  }
}

void tui_app_fails_closed_when_required_logger_is_missing() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  RecordingDataSource* const data_source_probe = data_source.get();
  std::ostringstream output;

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_required_logging";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.output_stream = &output;
  options.data_source_override = std::move(data_source);
  options.require_logger = true;
  options.logger_unavailable_reason = "compose_live_observability_failed";

  const int exit_code = app.run(std::move(options));

  assert_equal(1,
               exit_code,
               "production TUI should fail closed when required client logging is unavailable");
  assert_true(app.last_error().find("client logging unavailable") != std::string_view::npos,
              "required logger startup failure should be visible through last_error");
  assert_true(output.str().find("reason_code=compose_live_observability_failed") !=
                  std::string::npos,
              "required logger startup failure should expose a stable low-sensitive reason code");
  assert_true(data_source_probe->open_requests.empty(),
              "TUI should not open a foreground session when required logging is unavailable");
}

void tui_app_surfaces_logger_write_failures_without_crashing() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  data_source->submit_receipt = make_success_receipt();
  auto logger = std::make_shared<RecordingLogger>();
  logger->fail_log = true;
  logger->fail_flush = true;

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_logging_degraded";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.initial_draft = std::string("do not leak degraded logger prompt");
  options.scripted_actions.push_back(make_submit_action());
  options.data_source_override = std::move(data_source);
  options.logger = logger;

  const int exit_code = app.run(std::move(options));

  assert_equal(0,
               exit_code,
               "logger write failure should not crash an otherwise successful TUI flow");
  assert_true(app.logging_degraded(),
              "TUI should expose degraded logging state when ILogger returns failure");
  assert_true(app.logging_failure_count() > 0U,
              "TUI should count logger write/flush failures for diagnostics");
  assert_true(!app.logging_last_failure_stage().empty(),
              "TUI should retain the last low-sensitive logger failure stage");
  const auto& banners = app.screen_model().banners;
  assert_true(std::any_of(banners.begin(),
                          banners.end(),
                          [](const auto& banner) {
                            return banner.reason_code.has_value() &&
                                   *banner.reason_code == "client_logging_degraded";
                          }),
              "TUI should surface logger degradation as a user-visible warning banner");
  for (const auto& event : logger->events) {
    assert_true(!event_contains_text(event, "do not leak degraded logger prompt"),
                "TUI attempted log events must still avoid raw composer input under logger failure");
  }
}

void tui_app_records_foreground_clear_session_chain() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  RecordingDataSource* const data_source_probe = data_source.get();
  auto logger = std::make_shared<RecordingLogger>();

  TuiAction clear_action;
  clear_action.type = TuiActionType::ForegroundSessionClearRequested;
  clear_action.debug_reason = "scripted_logging_clear";

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_logging_clear";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.scripted_actions.push_back(clear_action);
  options.data_source_override = std::move(data_source);
  options.logger = logger;

  const int exit_code = app.run(std::move(options));

  assert_equal(0,
               exit_code,
               "foreground clear logging should preserve successful TUI execution");
  assert_true(data_source_probe->open_requests.size() >= 2U,
              "foreground clear should reopen a session after resetting local state");
  assert_true(std::any_of(data_source_probe->close_requests.begin(),
                          data_source_probe->close_requests.end(),
                          [](const auto& request) {
                            return request.close_reason == "/clear";
                          }),
              "foreground clear should explicitly close the previous foreground session");
  assert_true(find_event(logger->events, "tui.session.clear", "started") != nullptr,
              "TUI logging should record foreground clear start");
  assert_true(find_event(logger->events, "tui.session.clear", "success") != nullptr,
              "TUI logging should record foreground clear completion");
  assert_true(find_event_with_attr(logger->events,
                                   "tui.session.close",
                                   "close_reason",
                                   "/clear") != nullptr,
              "TUI logging should record the /clear close_session operation");
}

void tui_app_records_issue_metadata_with_redaction_compatible_keys() {
  auto data_source = std::make_unique<RecordingDataSource>();
  data_source->route_catalog_view = make_route_catalog();
  data_source->submit_issue = TuiDataSourceIssue{
      .reason_domain = "daemon",
      .reason_code = "daemon_unavailable",
      .message = "daemon unavailable for submit_turn",
      .retryable = true,
      .error_ref = std::optional<std::string>("error-ref-tui-logging"),
      .metadata = {{"operation", "submit_turn"},
                   {"socket_path", "/run/dasall/daemon.sock"},
                   {"authorization", "Bearer tui-secret-token"}},
  };
  auto logger = std::make_shared<RecordingLogger>();

  TuiApp app;
  TuiAppOptions options;
  options.scenario_id = "formal_logging_issue";
  options.probe_environment = make_full_screen_environment();
  options.print_final_screen = false;
  options.initial_draft = std::string("retry me without logging raw input");
  options.scripted_actions.push_back(make_submit_action());
  options.data_source_override = std::move(data_source);
  options.logger = logger;

  const int exit_code = app.run(std::move(options));

  assert_equal(0, exit_code,
               "TUI issue logging should not crash the app loop on retryable submit failure");
  const LogEvent* submit_failure = find_event(logger->events, "tui.turn.submit", "failure");
  assert_true(submit_failure != nullptr,
              "TUI logging should record submit failure as a structured event");
  assert_equal(std::string("daemon_unavailable"),
               submit_failure->attrs.at("issue.reason_code"),
               "submit failure log should carry stable reason code");
  assert_equal(std::string("true"),
               submit_failure->attrs.at("issue.retryable"),
               "submit failure log should carry retryable semantics");
  assert_equal(std::string("/run/dasall/daemon.sock"),
               submit_failure->attrs.at("issue.metadata.socket_path"),
               "submit failure log should preserve non-sensitive daemon metadata");

  const RedactionFilter redaction_filter;
  const LogEvent redacted = redaction_filter.apply(*submit_failure);
  assert_equal(std::string(LogEvent::kRedactedValue),
               redacted.attrs.at("issue.metadata.authorization"),
               "TUI issue metadata keys should remain compatible with infra redaction rules");
  assert_true(find_event(logger->events, "tui.issue", "failure") != nullptr,
              "TUI logging should also record the user-visible issue banner event");
}

}  // namespace

int main() {
  try {
    tui_app_records_low_sensitive_structured_events();
    tui_app_fails_closed_when_required_logger_is_missing();
    tui_app_surfaces_logger_write_failures_without_crashing();
    tui_app_records_foreground_clear_session_chain();
    tui_app_records_issue_metadata_with_redaction_compatible_keys();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}