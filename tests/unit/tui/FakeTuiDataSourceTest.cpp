#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "data/FakeTuiDataSource.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_FAKE_DATA_SOURCE_HEADER
#define DASALL_TUI_FAKE_DATA_SOURCE_HEADER "/home/gangan/DASALL/apps/tui/src/data/FakeTuiDataSource.h"
#endif

#ifndef DASALL_TUI_FAKE_DATA_SOURCE_IMPL
#define DASALL_TUI_FAKE_DATA_SOURCE_IMPL "/home/gangan/DASALL/apps/tui/src/data/FakeTuiDataSource.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeTuiDataSource;
using dasall::tui::data::TuiCloseSessionRequest;
using dasall::tui::data::TuiOpenSessionRequest;
using dasall::tui::data::TuiPollEventsRequest;
using dasall::tui::data::TuiPollEventsResult;
using dasall::tui::data::TuiRouteCatalogRequest;
using dasall::tui::data::TuiRouteCatalogResult;
using dasall::tui::data::TuiSubmitTurnRequest;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string optional_string(const std::optional<std::string>& value) {
  return value.value_or("<none>");
}

[[nodiscard]] std::string describe_route_catalog_result(const TuiRouteCatalogResult& result) {
  if (!result.route_catalog.has_value()) {
    return result.issue->reason_domain + "|" + result.issue->reason_code + "|" +
           result.issue->message;
  }

  std::ostringstream output;
  output << result.route_catalog->current_route.current_provider_id << '|'
         << result.route_catalog->current_route.current_model_id << '|'
         << result.route_catalog->current_route.current_depth_tier << '|'
         << result.route_catalog->current_route.next_preference.user_visible_summary << '|'
         << optional_string(result.route_catalog->current_route.next_preference.preferred_depth_tier);
  for (const auto& entry : result.route_catalog->candidate_routes) {
    output << '#' << entry.provider_id << '|' << entry.model_id << '|' << entry.depth_tier << '|'
           << (entry.selectable ? "true" : "false");
    for (const std::string& reason : entry.disabled_reasons) {
      output << '|' << reason;
    }
  }
  return output.str();
}

[[nodiscard]] std::string describe_poll_result(const TuiPollEventsResult& result) {
  std::ostringstream output;
  output << optional_string(result.next_cursor) << '|';
  if (result.issue.has_value()) {
    output << result.issue->reason_domain << '|' << result.issue->reason_code << '|'
           << result.issue->message;
    return output.str();
  }

  for (const auto& event : result.events) {
    output << '#' << event.event_cursor << '|' << event.event_kind << '|' << event.session_id << '|'
           << event.timestamp;
    if (event.status_delta.has_value()) {
      output << "|status:" << event.status_delta->stage << '|' << event.status_delta->current_tool
             << '|' << event.status_delta->pending_interaction << '|'
             << event.status_delta->budget_summary << '|'
             << event.status_delta->recovery_summary << '|'
             << event.status_delta->health_summary << '|'
             << event.status_delta->safe_mode_summary;
    }
    if (event.turn_receipt.has_value()) {
      output << "|receipt:" << event.turn_receipt->request_id << '|'
             << event.turn_receipt->trace_id << '|' << event.turn_receipt->session_id << '|'
             << event.turn_receipt->summary_text;
    }
    if (event.tool_summary.has_value()) {
      output << "|tool:" << event.tool_summary->tool_name << '|'
             << event.tool_summary->risk_summary << '|'
             << event.tool_summary->observation_summary;
    }
    output << "|banner:" << optional_string(event.banner_reason);
  }
  return output.str();
}

void fake_data_source_replays_deterministic_batches() {
  FakeTuiDataSource left("planning_tools");
  FakeTuiDataSource right("planning_tools");

  assert_true(left.has_loaded_scenario() && right.has_loaded_scenario(),
              "planning_tools should resolve to a valid fake scenario for both data sources");

  TuiOpenSessionRequest open_request;
  open_request.profile_id = std::string("desktop_full");
  open_request.startup_mode_hint = std::string("full");
  open_request.request_id = "req-open";
  open_request.trace_id = "trace-open";

  const auto left_open = left.open_session(open_request);
  const auto right_open = right.open_session(open_request);
  assert_true(left_open.ok() && right_open.ok(),
              "open_session should succeed for deterministic fake replay");
  assert_equal(left_open.session->session_id,
               right_open.session->session_id,
               "same fake scenario should expose the same session id across instances");

  TuiRouteCatalogRequest route_request;
  route_request.session_id = left_open.session->session_id;
  route_request.request_id = "req-route";
  route_request.trace_id = "trace-route";
  const auto left_route = left.route_catalog(route_request);
  const auto right_route = right.route_catalog(route_request);
  assert_true(left_route.ok() && right_route.ok(),
              "route_catalog should remain available after opening a fake session");
  assert_equal(describe_route_catalog_result(left_route),
               describe_route_catalog_result(right_route),
               "same fake scenario should expose the same route catalog across instances");

  TuiSubmitTurnRequest submit_request;
  submit_request.session_id = left_open.session->session_id;
  submit_request.user_input = "Plan the next action";
  submit_request.request_id = "req-submit";
  submit_request.trace_id = "trace-submit";
  submit_request.next_preference.user_visible_summary = "prefer depth";
  submit_request.next_preference.source = "test";

  const auto left_submit = left.submit_turn(submit_request);
  const auto right_submit = right.submit_turn(submit_request);
  assert_true(left_submit.ok() && right_submit.ok(),
              "submit_turn should succeed for the planning_tools scenario");
  assert_equal(left_submit.receipt->request_id,
               right_submit.receipt->request_id,
               "same fake scenario should reuse deterministic request correlation");
  assert_equal(std::string("req-submit"),
               left_submit.receipt->request_id,
               "submit_turn should preserve caller-visible request ids in the receipt");
  assert_true(left_submit.receipt->summary_text.find("tool execution") != std::string::npos,
              "planning_tools should keep a tool-oriented receipt summary");

  TuiPollEventsRequest first_poll_request;
  first_poll_request.session_id = left_open.session->session_id;
  first_poll_request.request_id = "req-poll-1";
  first_poll_request.trace_id = "trace-poll-1";

  const auto left_first_poll = left.poll_events(first_poll_request);
  const auto right_first_poll = right.poll_events(first_poll_request);
  assert_true(left_first_poll.ok() && right_first_poll.ok(),
              "first poll should emit the first deterministic planning batch");
  assert_equal(describe_poll_result(left_first_poll),
               describe_poll_result(right_first_poll),
               "first poll replay should stay identical across equal fake instances");
  assert_equal(static_cast<int>(1),
               static_cast<int>(left_first_poll.events.size()),
               "first planning batch should expose one status delta event");
  assert_equal(std::string("planning"),
               left_first_poll.events.front().status_delta->stage,
               "first planning batch should stay in the planning stage");

  TuiPollEventsRequest second_poll_request;
  second_poll_request.session_id = left_open.session->session_id;
  second_poll_request.event_cursor = left_first_poll.next_cursor;
  second_poll_request.request_id = "req-poll-2";
  second_poll_request.trace_id = "trace-poll-2";

  const auto left_second_poll = left.poll_events(second_poll_request);
  const auto right_second_poll = right.poll_events(second_poll_request);
  assert_true(left_second_poll.ok() && right_second_poll.ok(),
              "second poll should emit the second deterministic planning batch");
  assert_equal(describe_poll_result(left_second_poll),
               describe_poll_result(right_second_poll),
               "second poll replay should stay identical across equal fake instances");
  assert_true(left_second_poll.events.front().tool_summary.has_value(),
              "second planning batch should expose tool summary data");
  assert_true(left_second_poll.events.front().banner_reason.has_value(),
              "second planning batch should expose a busy-draft banner reason");

  TuiPollEventsRequest third_poll_request;
  third_poll_request.session_id = left_open.session->session_id;
  third_poll_request.event_cursor = left_second_poll.next_cursor;
  third_poll_request.request_id = "req-poll-3";
  third_poll_request.trace_id = "trace-poll-3";

  const auto left_third_poll = left.poll_events(third_poll_request);
  const auto right_third_poll = right.poll_events(third_poll_request);
  assert_true(left_third_poll.ok() && right_third_poll.ok(),
              "polling after the last deterministic batch should stay successful and empty");
  assert_true(left_third_poll.events.empty() && right_third_poll.events.empty(),
              "polling after replay exhaustion should not synthesize extra events");
  assert_equal(describe_poll_result(left_third_poll),
               describe_poll_result(right_third_poll),
               "post-exhaustion poll results should also stay deterministic");

  TuiCloseSessionRequest close_request;
  close_request.session_id = left_open.session->session_id;
  close_request.close_reason = "/exit";
  close_request.request_id = "req-close";
  close_request.trace_id = "trace-close";
  const auto left_close = left.close_session(close_request);
  const auto right_close = right.close_session(close_request);
  assert_true(left_close.ok() && right_close.ok(),
              "fake sessions should close cleanly after replay completes");
}

void fake_data_source_reports_machine_readable_errors() {
  FakeTuiDataSource missing("missing_scenario");
  assert_true(!missing.has_loaded_scenario(),
              "unknown scenario ids should keep the fake data source in a failed-load state");

  const auto missing_open = missing.open_session(TuiOpenSessionRequest{});
  assert_true(!missing_open.ok() && missing_open.issue.has_value(),
              "opening an unknown fake scenario should return a machine-readable issue");
  assert_equal(std::string("request"),
               missing_open.issue->reason_domain,
               "unknown scenarios should report request-scoped failures");
  assert_equal(std::string("validation_failed"),
               missing_open.issue->reason_code,
               "unknown scenarios should keep the stable validation_failed reason code");

  FakeTuiDataSource ready("golden_ready");
  const auto ready_open = ready.open_session(TuiOpenSessionRequest{});
  assert_true(ready_open.ok(), "golden_ready should open without extra input");

  TuiPollEventsRequest wrong_session_request;
  wrong_session_request.session_id = "wrong-session";
  wrong_session_request.request_id = "req-poll-wrong";
  wrong_session_request.trace_id = "trace-poll-wrong";
  const auto wrong_session_poll = ready.poll_events(wrong_session_request);

  assert_true(!wrong_session_poll.ok() && wrong_session_poll.issue.has_value(),
              "poll_events should reject unknown fake session ids");
  assert_equal(std::string("session"),
               wrong_session_poll.issue->reason_domain,
               "wrong fake session ids should report session-scoped failures");
  assert_equal(std::string("session_not_found"),
               wrong_session_poll.issue->reason_code,
               "wrong fake session ids should keep session_not_found stable");
}

void fake_data_source_files_avoid_transport_or_owner_private_dependencies() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_FAKE_DATA_SOURCE_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_FAKE_DATA_SOURCE_IMPL});

  assert_true(header_text.find("#include \"access/") == std::string::npos,
              "FakeTuiDataSource should not include access private headers");
  assert_true(header_text.find("#include \"runtime/") == std::string::npos,
              "FakeTuiDataSource should not include runtime private headers");
  assert_true(header_text.find("#include \"llm/") == std::string::npos,
              "FakeTuiDataSource should not include llm private headers");
  assert_true(header_text.find("#include \"profiles/") == std::string::npos,
              "FakeTuiDataSource should not include profile private headers");
  assert_true(header_text.find("ftxui") == std::string::npos,
              "FakeTuiDataSource should not leak renderer dependencies");

  assert_true(impl_text.find("#include <sys/socket.h>") == std::string::npos,
              "FakeTuiDataSource should not touch raw socket headers");
  assert_true(impl_text.find("socket(") == std::string::npos,
              "FakeTuiDataSource should not create sockets");
  assert_true(impl_text.find("connect(") == std::string::npos,
              "FakeTuiDataSource should not connect to transports");
  assert_true(impl_text.find("http://") == std::string::npos &&
                  impl_text.find("https://") == std::string::npos,
              "FakeTuiDataSource should not embed remote transport endpoints");
  assert_true(impl_text.find("TuiIpcController") == std::string::npos,
              "FakeTuiDataSource should remain decoupled from daemon IPC controllers");
}

}  // namespace

int main() {
  try {
    fake_data_source_replays_deterministic_batches();
    fake_data_source_reports_machine_readable_errors();
    fake_data_source_files_avoid_transport_or_owner_private_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiFakeDataSourceTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}