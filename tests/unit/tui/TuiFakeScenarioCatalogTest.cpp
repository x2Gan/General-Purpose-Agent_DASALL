#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "data/FakeScenarioCatalog.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_FAKE_SCENARIO_HEADER
#define DASALL_TUI_FAKE_SCENARIO_HEADER "/home/gangan/DASALL/apps/tui/src/data/FakeScenarioCatalog.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeScenario;
using dasall::tui::data::FakeScenarioCatalog;
using dasall::tui::data::FakeScenarioLoadResult;
using dasall::tui::data::TuiDataSourceIssue;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogView;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string optional_string(const std::optional<std::string>& value) {
  return value.value_or("<none>");
}

[[nodiscard]] std::string bool_string(bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string join_strings(const std::vector<std::string>& values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    output << values[index];
  }
  return output.str();
}

[[nodiscard]] std::string describe_route_catalog_entry(const TuiRouteCatalogEntry& entry) {
  std::ostringstream output;
  output << entry.provider_id << '|' << entry.model_id << '|' << entry.depth_tier << '|'
         << bool_string(entry.selectable) << '|' << join_strings(entry.disabled_reasons);
  return output.str();
}

[[nodiscard]] std::string describe_route_catalog(const TuiRouteCatalogView& route_catalog) {
  std::ostringstream output;
  output << route_catalog.current_route.current_provider_id << '|'
         << route_catalog.current_route.current_model_id << '|'
         << route_catalog.current_route.current_depth_tier << '|'
         << route_catalog.current_route.next_preference.user_visible_summary << '|'
         << route_catalog.current_route.next_preference.source << '|'
         << optional_string(route_catalog.current_route.next_preference.preferred_depth_tier)
         << '|'
         << join_strings(route_catalog.disabled_reasons);

  for (const TuiRouteCatalogEntry& entry : route_catalog.candidate_routes) {
    output << "#" << describe_route_catalog_entry(entry);
  }

  return output.str();
}

[[nodiscard]] std::string describe_issue(const std::optional<TuiDataSourceIssue>& issue) {
  if (!issue.has_value()) {
    return "<none>";
  }

  std::ostringstream output;
  output << issue->reason_domain << '|' << issue->reason_code << '|' << issue->message << '|'
         << bool_string(issue->retryable) << '|' << optional_string(issue->error_ref);
  for (const auto& [key, value] : issue->metadata) {
    output << '#' << key << '=' << value;
  }
  return output.str();
}

[[nodiscard]] std::string describe_event(const TuiEventProjection& event) {
  std::ostringstream output;
  output << event.event_cursor << '|' << event.event_kind << '|' << event.session_id << '|'
         << event.timestamp;
  if (event.status_delta.has_value()) {
    output << "|status:" << event.status_delta->stage << '|' << event.status_delta->current_tool
           << '|' << event.status_delta->pending_interaction << '|'
           << event.status_delta->budget_summary << '|' << event.status_delta->recovery_summary
           << '|' << event.status_delta->health_summary << '|'
           << event.status_delta->safe_mode_summary;
  }
  if (event.turn_receipt.has_value()) {
    output << "|receipt:" << event.turn_receipt->request_id << '|'
           << event.turn_receipt->trace_id << '|' << event.turn_receipt->session_id << '|'
           << event.turn_receipt->disposition << '|' << event.turn_receipt->receipt_ref << '|'
           << event.turn_receipt->submitted_at << '|' << event.turn_receipt->summary_text << '|'
           << optional_string(event.turn_receipt->reason_code);
  }
  if (event.tool_summary.has_value()) {
    output << "|tool:" << event.tool_summary->tool_name << '|'
           << event.tool_summary->risk_summary << '|'
           << event.tool_summary->observation_summary << '|';
    if (event.tool_summary->latency_ms.has_value()) {
      output << *event.tool_summary->latency_ms;
    } else {
      output << "<none>";
    }
    output << '|' << join_strings(event.tool_summary->badges);
  }
  output << "|banner:" << optional_string(event.banner_reason);
  return output.str();
}

[[nodiscard]] std::string describe_scenario(const FakeScenario& scenario) {
  std::ostringstream output;
  output << scenario.scenario_id << '|' << scenario.session.session_id << '|'
         << scenario.session.profile_id << '|' << scenario.session.daemon_readiness << '|'
         << scenario.session.startup_mode << '|' << scenario.session.started_at << '|'
         << describe_route_catalog(scenario.route_catalog) << '|';

  if (scenario.submit_result.receipt.has_value()) {
    output << scenario.submit_result.receipt->request_id << '|'
           << scenario.submit_result.receipt->trace_id << '|'
           << scenario.submit_result.receipt->summary_text;
  } else {
    output << describe_issue(scenario.submit_result.issue);
  }

  output << '|';
  for (const auto& batch : scenario.event_batches) {
    output << "batch";
    for (const TuiEventProjection& event : batch) {
      output << '#' << describe_event(event);
    }
    output << '|';
  }
  return output.str();
}

void catalog_exposes_all_required_scenarios() {
  const auto scenario_ids = FakeScenarioCatalog::scenario_ids();
  std::set<std::string> unique_ids;

  for (const std::string_view scenario_id : scenario_ids) {
    unique_ids.insert(std::string(scenario_id));
    const FakeScenarioLoadResult load_result = FakeScenarioCatalog::load(scenario_id);

    assert_true(load_result.ok() && load_result.has_consistent_values(),
                "each required fake scenario should load without ambiguity");
    assert_true(load_result.scenario->has_consistent_values(),
                "each required fake scenario should keep session, route and receipt shape consistent");
  }

  assert_equal(static_cast<int>(6),
               static_cast<int>(scenario_ids.size()),
               "fake scenario catalog should expose the six required scenario ids");
  assert_equal(static_cast<int>(6),
               static_cast<int>(unique_ids.size()),
               "fake scenario catalog should not duplicate scenario ids");

  const FakeScenarioLoadResult planning = FakeScenarioCatalog::load("planning_tools");
  assert_equal(static_cast<int>(2),
               static_cast<int>(planning.scenario->event_batches.size()),
               "planning_tools should expose a multi-batch deterministic timeline");

  const FakeScenarioLoadResult route_switch = FakeScenarioCatalog::load("route_switch");
  const bool has_disabled_candidate = std::any_of(
      route_switch.scenario->route_catalog.candidate_routes.begin(),
      route_switch.scenario->route_catalog.candidate_routes.end(),
      [](const TuiRouteCatalogEntry& entry) {
        return !entry.selectable && !entry.disabled_reasons.empty();
      });
  assert_true(has_disabled_candidate,
              "route_switch should expose selector-disabled reasons for the modal flow");

  const FakeScenarioLoadResult narrow = FakeScenarioCatalog::load("narrow_cjk");
  assert_true(narrow.scenario->submit_result.receipt->summary_text.find("中文") != std::string::npos,
              "narrow_cjk should keep a CJK receipt summary for narrow terminal replay");
}

void catalog_replay_is_deterministic_and_machine_readable_on_failure() {
  const FakeScenarioLoadResult first_load = FakeScenarioCatalog::load("recovering");
  const FakeScenarioLoadResult second_load = FakeScenarioCatalog::load("recovering");

  assert_equal(describe_scenario(*first_load.scenario),
               describe_scenario(*second_load.scenario),
               "loading the same fake scenario twice should produce identical replay data");

  const FakeScenarioLoadResult missing = FakeScenarioCatalog::load("missing_scenario");
  assert_true(!missing.ok() && missing.has_consistent_values(),
              "unknown fake scenarios should fail with a machine-readable issue only");
  assert_equal(std::string("request"),
               missing.issue->reason_domain,
               "unknown fake scenarios should report the request failure domain");
  assert_equal(std::string("validation_failed"),
               missing.issue->reason_code,
               "unknown fake scenarios should stay on the stable validation_failed reason code");
}

void fake_scenario_header_avoids_transport_and_owner_private_includes() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_FAKE_SCENARIO_HEADER});

  assert_true(header_text.find("#include \"access/") == std::string::npos,
              "FakeScenarioCatalog should not include access private headers");
  assert_true(header_text.find("#include \"runtime/") == std::string::npos,
              "FakeScenarioCatalog should not include runtime private headers");
  assert_true(header_text.find("#include \"llm/") == std::string::npos,
              "FakeScenarioCatalog should not include llm private headers");
  assert_true(header_text.find("#include \"profiles/") == std::string::npos,
              "FakeScenarioCatalog should not include profile private headers");
  assert_true(header_text.find("ftxui") == std::string::npos,
              "FakeScenarioCatalog should not leak renderer dependencies into fake data fixtures");
  assert_true(header_text.find("socket(") == std::string::npos,
              "FakeScenarioCatalog should remain a pure data catalog without socket calls");
  assert_true(header_text.find("connect(") == std::string::npos,
              "FakeScenarioCatalog should not initiate transport connections");
}

}  // namespace

int main() {
  try {
    catalog_exposes_all_required_scenarios();
    catalog_replay_is_deterministic_and_machine_readable_on_failure();
    fake_scenario_header_avoids_transport_and_owner_private_includes();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiFakeScenarioCatalogTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}