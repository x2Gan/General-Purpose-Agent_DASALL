#include "data/FakeTuiDataSource.h"

#include <utility>

namespace dasall::tui::data {

FakeTuiDataSource::FakeTuiDataSource(std::string_view scenario_id)
    : requested_scenario_id_(scenario_id) {
  FakeScenarioLoadResult load_result = FakeScenarioCatalog::load(scenario_id);
  scenario_ = std::move(load_result.scenario);
  scenario_issue_ = std::move(load_result.issue);
}

FakeTuiDataSource::FakeTuiDataSource(FakeScenario scenario)
    : requested_scenario_id_(scenario.scenario_id), scenario_(std::move(scenario)) {}

std::string_view FakeTuiDataSource::scenario_id() const {
  return requested_scenario_id_;
}

bool FakeTuiDataSource::has_loaded_scenario() const {
  return scenario_.has_value() && !scenario_issue_.has_value();
}

TuiOpenSessionResult FakeTuiDataSource::open_session(const TuiOpenSessionRequest& request) {
  if (std::optional<TuiDataSourceIssue> issue = validate_loaded_scenario()) {
    return TuiOpenSessionResult{std::nullopt, std::move(issue)};
  }

  current_session_ = scenario_->session;
  if (request.profile_id.has_value() && !request.profile_id->empty()) {
    current_session_->profile_id = *request.profile_id;
  }
  if (request.startup_mode_hint.has_value() && !request.startup_mode_hint->empty()) {
    current_session_->startup_mode = *request.startup_mode_hint;
  }

  next_event_batch_index_ = 0;
  last_event_cursor_.reset();
  session_open_ = true;
  return TuiOpenSessionResult{current_session_, std::nullopt};
}

TuiSubmitTurnResult FakeTuiDataSource::submit_turn(const TuiSubmitTurnRequest& request) {
  if (std::optional<TuiDataSourceIssue> issue = validate_session_id(request.session_id)) {
    return TuiSubmitTurnResult{std::nullopt, std::move(issue)};
  }

  TuiSubmitTurnResult result = scenario_->submit_result;
  if (result.receipt.has_value()) {
    result.receipt->session_id = current_session_->session_id;
    if (!request.request_id.empty()) {
      result.receipt->request_id = request.request_id;
    }
    if (!request.trace_id.empty()) {
      result.receipt->trace_id = request.trace_id;
    }
  }
  return result;
}

TuiPollEventsResult FakeTuiDataSource::poll_events(const TuiPollEventsRequest& request) {
  if (std::optional<TuiDataSourceIssue> issue = validate_session_id(request.session_id)) {
    return TuiPollEventsResult{{}, std::nullopt, std::move(issue)};
  }

  if (request.event_cursor.has_value()) {
    if (!last_event_cursor_.has_value()) {
      return TuiPollEventsResult{
          {},
          std::nullopt,
          detail::make_issue("request",
                             "validation_failed",
                             "Event cursor replay requested before any fake event was emitted.",
                             {{"scenario_id", requested_scenario_id_},
                              {"received_cursor", *request.event_cursor}})};
    }

    if (request.event_cursor != last_event_cursor_) {
      return TuiPollEventsResult{
          {},
          std::nullopt,
          detail::make_issue("request",
                             "validation_failed",
                             "Unexpected event cursor for fake replay.",
                             {{"scenario_id", requested_scenario_id_},
                              {"expected_cursor", *last_event_cursor_},
                              {"received_cursor", *request.event_cursor}})};
    }
  }

  if (next_event_batch_index_ >= scenario_->event_batches.size()) {
    return TuiPollEventsResult{{}, last_event_cursor_, std::nullopt};
  }

  std::vector<TuiEventProjection> events = scenario_->event_batches.at(next_event_batch_index_++);
  for (TuiEventProjection& event : events) {
    event.session_id = current_session_->session_id;
  }

  if (!events.empty()) {
    last_event_cursor_ = events.back().event_cursor;
  }

  return TuiPollEventsResult{std::move(events), last_event_cursor_, std::nullopt};
}

TuiRouteCatalogResult FakeTuiDataSource::route_catalog(const TuiRouteCatalogRequest& request) {
  if (std::optional<TuiDataSourceIssue> issue = validate_loaded_scenario()) {
    return TuiRouteCatalogResult{std::nullopt, std::move(issue)};
  }

  if (request.session_id.has_value()) {
    if (std::optional<TuiDataSourceIssue> issue = validate_session_id(*request.session_id)) {
      return TuiRouteCatalogResult{std::nullopt, std::move(issue)};
    }
  }

  return TuiRouteCatalogResult{scenario_->route_catalog, std::nullopt};
}

TuiCloseSessionResult FakeTuiDataSource::close_session(const TuiCloseSessionRequest& request) {
  if (std::optional<TuiDataSourceIssue> issue = validate_session_id(request.session_id)) {
    return TuiCloseSessionResult{false, std::move(issue)};
  }

  session_open_ = false;
  next_event_batch_index_ = 0;
  last_event_cursor_.reset();
  current_session_.reset();
  return TuiCloseSessionResult{true, std::nullopt};
}

std::optional<TuiDataSourceIssue> FakeTuiDataSource::validate_loaded_scenario() const {
  if (scenario_.has_value() && !scenario_issue_.has_value()) {
    return std::nullopt;
  }

  if (scenario_issue_.has_value()) {
    return scenario_issue_;
  }

  return detail::make_issue("request",
                            "validation_failed",
                            "Fake TUI scenario is unavailable.",
                            {{"scenario_id", requested_scenario_id_}});
}

std::optional<TuiDataSourceIssue> FakeTuiDataSource::validate_session_id(std::string_view session_id) const {
  if (std::optional<TuiDataSourceIssue> issue = validate_loaded_scenario()) {
    return issue;
  }

  if (session_id.empty()) {
    return detail::make_issue("request",
                              "validation_failed",
                              "Session id is required for fake TUI replay.",
                              {{"scenario_id", requested_scenario_id_}});
  }

  if (!session_open_ || !current_session_.has_value()) {
    return detail::make_issue("session",
                              "session_not_open",
                              "Fake TUI session has not been opened yet.",
                              {{"scenario_id", requested_scenario_id_}});
  }

  if (session_id != current_session_->session_id) {
    return detail::make_issue("session",
                              "session_not_found",
                              "Unknown fake TUI session id requested.",
                              {{"scenario_id", requested_scenario_id_},
                               {"expected_session_id", current_session_->session_id},
                               {"received_session_id", std::string(session_id)}});
  }

  return std::nullopt;
}

}  // namespace dasall::tui::data