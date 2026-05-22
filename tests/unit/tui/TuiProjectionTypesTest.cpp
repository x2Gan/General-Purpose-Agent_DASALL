#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "data/TuiProjectionTypes.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_PROJECTION_TYPES_HEADER
#define DASALL_TUI_PROJECTION_TYPES_HEADER "/home/gangan/DASALL/apps/tui/src/data/TuiProjectionTypes.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiEventProjection;
using dasall::tui::data::TuiModelRouteProjection;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::data::TuiSessionView;
using dasall::tui::data::TuiStatusProjection;
using dasall::tui::data::TuiToolSummaryView;
using dasall::tui::data::TuiTurnReceipt;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void projection_types_expose_expected_default_shape() {
  const NextTurnPreference preference;
  const TuiSessionView session;
  const TuiTurnReceipt receipt;
  const TuiStatusProjection status;
  const TuiToolSummaryView tool_summary;
  const TuiModelRouteProjection route;
  const TuiRouteCatalogView catalog;
  const TuiEventProjection event;

  assert_equal(static_cast<int>(TuiRoutePreferenceMode::Auto),
               static_cast<int>(preference.mode),
               "NextTurnPreference should default to Auto mode");
  assert_true(!preference.preferred_depth_tier.has_value(),
              "NextTurnPreference should not force a depth tier by default");
  assert_true(!preference.pinned_provider_id.has_value(),
              "NextTurnPreference should not pin a provider by default");
  assert_true(!preference.pinned_model_id.has_value(),
              "NextTurnPreference should not pin a model by default");
  assert_true(preference.user_visible_summary.empty(),
              "NextTurnPreference should allow the selector to fill summary text later");
  assert_true(preference.source.empty(),
              "NextTurnPreference should keep the source unset by default");
  assert_true(preference.applies_to_next_turn_only,
              "NextTurnPreference should remain next-turn-only by default");

  assert_true(session.session_id.empty() && session.profile_id.empty(),
              "TuiSessionView should remain a pure DTO with empty string defaults");
  assert_true(receipt.summary_text.empty() && !receipt.reason_code.has_value(),
              "TuiTurnReceipt should expose summary and optional reason_code separately");
  assert_true(status.stage.empty() && status.health_summary.empty(),
              "TuiStatusProjection should default to an empty summary shape");
  assert_true(tool_summary.badges.empty() && !tool_summary.latency_ms.has_value(),
              "TuiToolSummaryView should keep badges and latency optional by default");
  assert_true(route.disabled_reasons.empty(),
              "TuiModelRouteProjection should keep disabled reasons additive");
  assert_true(route.next_preference.applies_to_next_turn_only,
              "TuiModelRouteProjection should embed the same next-turn-only preference contract");
  assert_true(catalog.candidate_routes.empty() && catalog.disabled_reasons.empty(),
              "TuiRouteCatalogView should start as an empty supporting projection");
  assert_true(!event.status_delta.has_value() && !event.turn_receipt.has_value() &&
                  !event.tool_summary.has_value() && !event.banner_reason.has_value(),
              "TuiEventProjection should keep partial deltas optional");
}

void projection_types_header_avoids_owner_private_includes() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_PROJECTION_TYPES_HEADER});

  assert_true(header_text.find("access/") == std::string::npos,
              "TuiProjectionTypes should not include access private headers");
  assert_true(header_text.find("runtime/") == std::string::npos,
              "TuiProjectionTypes should not include runtime private headers");
  assert_true(header_text.find("llm/") == std::string::npos,
              "TuiProjectionTypes should not include llm private headers");
  assert_true(header_text.find("profiles/") == std::string::npos,
              "TuiProjectionTypes should not include profile private headers");
  assert_true(header_text.find("ftxui") == std::string::npos,
              "TuiProjectionTypes should not leak FTXUI into DTO definitions");
  assert_true(header_text.find("AgentRequest") == std::string::npos,
              "TuiProjectionTypes should not bind to access shared request owners");
  assert_true(header_text.find("RuntimeDispatchRequest") == std::string::npos,
              "TuiProjectionTypes should not bind to runtime dispatch owners");
}

}  // namespace

int main() {
  try {
    projection_types_expose_expected_default_shape();
    projection_types_header_avoids_owner_private_includes();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiProjectionTypesTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}