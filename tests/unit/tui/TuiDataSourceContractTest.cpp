#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "data/ITuiDataSource.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_DATA_SOURCE_HEADER
#define DASALL_TUI_DATA_SOURCE_HEADER "/home/gangan/DASALL/apps/tui/src/data/ITuiDataSource.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::ITuiDataSource;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiCloseSessionRequest;
using dasall::tui::data::TuiCloseSessionResult;
using dasall::tui::data::TuiDataSourceIssue;
using dasall::tui::data::TuiEventProjection;
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

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

class StubTuiDataSource final : public ITuiDataSource {
 public:
  TuiOpenSessionResult open_result;
  TuiSubmitTurnResult submit_result;
  TuiPollEventsResult poll_result;
  TuiRouteCatalogResult route_result;
  TuiCloseSessionResult close_result;

  TuiOpenSessionRequest last_open_request;
  TuiSubmitTurnRequest last_submit_request;
  TuiPollEventsRequest last_poll_request;
  TuiRouteCatalogRequest last_route_request;
  TuiCloseSessionRequest last_close_request;

  TuiOpenSessionResult open_session(const TuiOpenSessionRequest& request) override {
    last_open_request = request;
    return open_result;
  }

  TuiSubmitTurnResult submit_turn(const TuiSubmitTurnRequest& request) override {
    last_submit_request = request;
    return submit_result;
  }

  TuiPollEventsResult poll_events(const TuiPollEventsRequest& request) override {
    last_poll_request = request;
    return poll_result;
  }

  TuiRouteCatalogResult route_catalog(const TuiRouteCatalogRequest& request) override {
    last_route_request = request;
    return route_result;
  }

  TuiCloseSessionResult close_session(const TuiCloseSessionRequest& request) override {
    last_close_request = request;
    return close_result;
  }
};

void interface_exposes_all_five_operations_with_module_local_types() {
  StubTuiDataSource data_source;

  TuiSessionView session;
  session.session_id = "session-001";
  session.profile_id = "desktop_full";
  session.daemon_readiness = "ready";
  session.startup_mode = "full";
  session.started_at = "2026-05-22T09:00:00Z";
  data_source.open_result.session = session;

  TuiTurnReceipt receipt;
  receipt.request_id = "req-submit";
  receipt.trace_id = "trace-submit";
  receipt.session_id = session.session_id;
  receipt.disposition = "accepted_async";
  receipt.receipt_ref = "receipt-001";
  receipt.submitted_at = "2026-05-22T09:00:01Z";
  receipt.summary_text = "turn accepted";
  data_source.submit_result.receipt = receipt;

  TuiEventProjection event;
  event.event_cursor = "cursor-002";
  event.event_kind = "status.updated";
  event.session_id = session.session_id;
  event.timestamp = "2026-05-22T09:00:02Z";
  data_source.poll_result.events.push_back(event);
  data_source.poll_result.next_cursor = event.event_cursor;

  TuiRouteCatalogEntry route_entry;
  route_entry.provider_id = "provider-openai";
  route_entry.model_id = "gpt-4.1";
  route_entry.depth_tier = "balanced";

  TuiRouteCatalogView route_catalog;
  route_catalog.current_route.current_provider_id = route_entry.provider_id;
  route_catalog.current_route.current_model_id = route_entry.model_id;
  route_catalog.current_route.current_depth_tier = route_entry.depth_tier;
  route_catalog.current_route.next_preference.user_visible_summary = "auto";
  route_catalog.candidate_routes.push_back(route_entry);
  data_source.route_result.route_catalog = route_catalog;

  data_source.close_result.closed = true;

  TuiOpenSessionRequest open_request;
  open_request.profile_id = std::string("desktop_full");
  open_request.startup_mode_hint = std::string("full");
  open_request.request_id = "req-open";
  open_request.trace_id = "trace-open";

  TuiSubmitTurnRequest submit_request;
  submit_request.session_id = session.session_id;
  submit_request.user_input = "Summarize current status";
  submit_request.next_preference.mode = TuiRoutePreferenceMode::PreferDepth;
  submit_request.next_preference.preferred_depth_tier = std::string("deep");
  submit_request.next_preference.user_visible_summary = "prefer depth";
  submit_request.request_id = "req-submit";
  submit_request.trace_id = "trace-submit";

  TuiPollEventsRequest poll_request;
  poll_request.session_id = session.session_id;
  poll_request.event_cursor = std::string("cursor-001");
  poll_request.request_id = "req-poll";
  poll_request.trace_id = "trace-poll";

  TuiRouteCatalogRequest route_request;
  route_request.session_id = session.session_id;
  route_request.profile_id = session.profile_id;
  route_request.selector_mode = std::string("next_turn");
  route_request.request_id = "req-route";
  route_request.trace_id = "trace-route";

  TuiCloseSessionRequest close_request;
  close_request.session_id = session.session_id;
  close_request.close_reason = "/exit";
  close_request.request_id = "req-close";
  close_request.trace_id = "trace-close";

  const TuiOpenSessionResult open_result = data_source.open_session(open_request);
  const TuiSubmitTurnResult submit_result = data_source.submit_turn(submit_request);
  const TuiPollEventsResult poll_result = data_source.poll_events(poll_request);
  const TuiRouteCatalogResult route_result = data_source.route_catalog(route_request);
  const TuiCloseSessionResult close_result = data_source.close_session(close_request);

  assert_true(open_result.ok() && open_result.has_consistent_values(),
              "open_session should expose a successful session projection");
  assert_true(submit_result.ok() && submit_result.has_consistent_values(),
              "submit_turn should expose a successful turn receipt projection");
  assert_true(poll_result.ok() && poll_result.has_consistent_values(),
              "poll_events should expose a consistent event batch contract");
  assert_true(route_result.ok() && route_result.has_consistent_values(),
              "route_catalog should expose a successful route projection contract");
  assert_true(close_result.ok() && close_result.has_consistent_values(),
              "close_session should expose a successful close contract");

  assert_equal(std::string("req-open"),
               data_source.last_open_request.request_id,
               "open_session should preserve request identity fields");
  assert_equal(std::string("Summarize current status"),
               data_source.last_submit_request.user_input,
               "submit_turn should preserve user input for fake and daemon seams alike");
  assert_equal(std::string("cursor-001"),
               data_source.last_poll_request.event_cursor.value_or(std::string()),
               "poll_events should preserve the prior cursor when present");
  assert_equal(std::string("next_turn"),
               data_source.last_route_request.selector_mode.value_or(std::string()),
               "route_catalog should preserve selector context hints");
  assert_equal(std::string("/exit"),
               data_source.last_close_request.close_reason,
               "close_session should preserve the caller-visible close reason");
}

void issue_and_result_contracts_stay_machine_readable() {
  TuiDataSourceIssue issue;
  issue.reason_domain = "transport";
  issue.reason_code = "permission_denied";
  issue.message = "socket ownership mismatch";
  issue.retryable = false;
  issue.error_ref = std::string("err-001");
  issue.metadata.emplace_back("socket_path", "/run/dasall/daemon.sock");

  assert_true(issue.has_reason() && issue.has_consistent_values(),
              "data source issues should keep machine-readable reason fields stable");

  TuiDataSourceIssue invalid_issue;
  invalid_issue.message = "missing reason code";
  assert_true(!invalid_issue.has_consistent_values(),
              "data source issues should not allow free-form message-only failures");

  TuiOpenSessionResult open_failure;
  open_failure.issue = issue;
  assert_true(!open_failure.ok() && open_failure.has_consistent_values(),
              "open_session failures should remain distinguishable from successful session bootstraps");

  TuiOpenSessionResult inconsistent_open;
  inconsistent_open.session = TuiSessionView{};
  inconsistent_open.issue = issue;
  assert_true(!inconsistent_open.has_consistent_values(),
              "open_session should reject payload-plus-error ambiguity");

  TuiPollEventsResult poll_failure;
  poll_failure.events.push_back(TuiEventProjection{});
  poll_failure.next_cursor = std::string("cursor-002");
  poll_failure.issue = issue;
  assert_true(!poll_failure.has_consistent_values(),
              "poll_events should not surface partial event data together with a terminal error");

  TuiCloseSessionResult close_failure;
  close_failure.issue = issue;
  assert_true(!close_failure.ok() && close_failure.has_consistent_values(),
              "close_session failures should stay machine-readable without pretending success");
}

void data_source_header_avoids_owner_private_includes_and_renderer_deps() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_DATA_SOURCE_HEADER});

  assert_true(header_text.find("access/") == std::string::npos,
              "ITuiDataSource should not include access private headers");
  assert_true(header_text.find("runtime/") == std::string::npos,
              "ITuiDataSource should not include runtime private headers");
  assert_true(header_text.find("llm/") == std::string::npos,
              "ITuiDataSource should not include llm private headers");
  assert_true(header_text.find("profiles/") == std::string::npos,
              "ITuiDataSource should not include profile private headers");
  assert_true(header_text.find("ftxui") == std::string::npos,
              "ITuiDataSource should not leak FTXUI into the data source seam");
  assert_true(header_text.find("TuiIpcController") == std::string::npos,
              "ITuiDataSource should not bind directly to daemon IPC implementation details");
  assert_true(header_text.find("AgentRequest") == std::string::npos,
              "ITuiDataSource should not bind to access shared request owners");
  assert_true(header_text.find("RuntimeDispatchRequest") == std::string::npos,
              "ITuiDataSource should not bind to runtime dispatch owners");
}

}  // namespace

int main() {
  try {
    interface_exposes_all_five_operations_with_module_local_types();
    issue_and_result_contracts_stay_machine_readable();
    data_source_header_avoids_owner_private_includes_and_renderer_deps();
  } catch (const std::exception& exception) {
    std::cerr << "[ITuiDataSourceContractTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}