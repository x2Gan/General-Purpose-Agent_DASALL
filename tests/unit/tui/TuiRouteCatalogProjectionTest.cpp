#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "data/FakeScenarioCatalog.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_PROJECTION_TYPES_HEADER
#define DASALL_TUI_PROJECTION_TYPES_HEADER \
  "/home/gangan/DASALL/apps/tui/src/data/TuiProjectionTypes.h"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeScenarioCatalog;
using dasall::tui::data::TuiRouteCatalogEntry;
using dasall::tui::data::TuiRouteCatalogView;
using dasall::tui::data::TuiRoutePreferenceMode;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] const TuiRouteCatalogEntry* find_route_entry(
    const TuiRouteCatalogView& route_catalog,
    const std::string& provider_id,
    const std::string& model_id) {
  for (const TuiRouteCatalogEntry& entry : route_catalog.candidate_routes) {
    if (entry.provider_id == provider_id && entry.model_id == model_id) {
      return &entry;
    }
  }

  return nullptr;
}

void route_catalog_projection_freezes_current_route_and_candidate_guards() {
  const auto scenario = FakeScenarioCatalog::load("route_switch");
  assert_true(scenario.ok(),
              "route_switch should provide a deterministic route catalog fixture");

  const TuiRouteCatalogView& route_catalog = scenario.scenario->route_catalog;

  assert_equal(std::string("provider-openai"),
               route_catalog.current_route.current_provider_id,
               "current route should preserve the effective provider id");
  assert_equal(std::string("gpt-4.1"),
               route_catalog.current_route.current_model_id,
               "current route should preserve the effective model id");
  assert_equal(std::string("balanced"),
               route_catalog.current_route.current_depth_tier,
               "current route should preserve the effective depth tier");
  assert_equal(std::string("verified"),
               route_catalog.current_route.verification_state,
               "current route should surface verification state separately from disabled reasons");
  assert_equal(std::string("healthy"),
               route_catalog.current_route.health,
               "current route should surface health separately from disabled reasons");
  assert_true(route_catalog.current_route.profile_allowlisted,
              "current route should explicitly report whether the effective profile allowlists it");
  assert_true(route_catalog.current_route.next_preference.mode ==
                  TuiRoutePreferenceMode::PreferDepth &&
                  route_catalog.current_route.next_preference.preferred_depth_tier ==
                      std::optional<std::string>("deep"),
              "route catalog should preserve the next-turn preference draft that will be echoed later");

  const TuiRouteCatalogEntry* local_entry =
      find_route_entry(route_catalog, "provider-local", "deep-reasoner");
  assert_true(local_entry != nullptr,
              "route catalog should keep disabled local pin candidates visible");
  assert_equal(std::string("pending"),
               local_entry->verification_state,
               "disabled local routes should expose verification state separately");
  assert_equal(std::string("healthy"),
               local_entry->health,
               "route catalog should keep health as a separate summary field");
  assert_true(!local_entry->profile_allowlisted,
              "route catalog should explicitly surface profile allowlist denials");
  assert_true(!local_entry->selectable,
              "allowlist or verification failures should keep the candidate non-selectable");
  assert_true(local_entry->disabled_reasons.size() == 2 &&
                  local_entry->disabled_reasons[0] == "verification_pending" &&
                  local_entry->disabled_reasons[1] == "allowlist_blocked",
              "disabled reasons should remain additive even when explicit projection fields are present");
}

void route_catalog_projection_can_express_fail_closed_route_state_without_dumping_policy() {
  TuiRouteCatalogView route_catalog;
  route_catalog.current_route.current_provider_id = "provider-openai";
  route_catalog.current_route.current_model_id = "gpt-4.1";
  route_catalog.current_route.current_depth_tier = "balanced";
  route_catalog.current_route.verification_state = "verified";
  route_catalog.current_route.health = "healthy";
  route_catalog.current_route.profile_allowlisted = true;
  route_catalog.current_route.next_preference.user_visible_summary = "auto";

  TuiRouteCatalogEntry unhealthy_route;
  unhealthy_route.provider_id = "provider-azure";
  unhealthy_route.model_id = "gpt-4o";
  unhealthy_route.depth_tier = "balanced";
  unhealthy_route.verification_state = "verified";
  unhealthy_route.health = "degraded";
  unhealthy_route.profile_allowlisted = true;
  unhealthy_route.selectable = false;
  unhealthy_route.disabled_reasons = {"provider_unhealthy"};
  route_catalog.candidate_routes.push_back(unhealthy_route);

  TuiRouteCatalogEntry disallowed_route;
  disallowed_route.provider_id = "provider-local";
  disallowed_route.model_id = "offline-preview";
  disallowed_route.depth_tier = "deep";
  disallowed_route.verification_state = "verified";
  disallowed_route.health = "healthy";
  disallowed_route.profile_allowlisted = false;
  disallowed_route.selectable = false;
  disallowed_route.disabled_reasons = {"allowlist_blocked"};
  route_catalog.candidate_routes.push_back(disallowed_route);

  assert_equal(std::string("degraded"),
               route_catalog.candidate_routes[0].health,
               "route catalog should express health-driven fail-closed candidates explicitly");
  assert_true(route_catalog.candidate_routes[0].profile_allowlisted,
              "health failures should stay separate from allowlist decisions");
  assert_true(!route_catalog.candidate_routes[1].profile_allowlisted,
              "route catalog should express allowlist denials without requiring full profile dumps");
  assert_equal(std::string("allowlist_blocked"),
               route_catalog.candidate_routes[1].disabled_reasons.front(),
               "allowlist denials should remain machine-readable reason codes");
}

void route_catalog_projection_header_stays_summary_only() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_PROJECTION_TYPES_HEADER});

  assert_true(header_text.find("verification_state") != std::string::npos,
              "route catalog projection should freeze verification_state as an explicit field");
  assert_true(header_text.find("health") != std::string::npos,
              "route catalog projection should freeze health as an explicit field");
  assert_true(header_text.find("profile_allowlisted") != std::string::npos,
              "route catalog projection should freeze profile allowlist as an explicit field");
  assert_true(header_text.find("api_key") == std::string::npos,
              "route catalog projection must not expose provider secret fields");
  assert_true(header_text.find("secret") == std::string::npos,
              "route catalog projection must not expose provider secret payloads");
  assert_true(header_text.find("profile_path") == std::string::npos,
              "route catalog projection must not expose profile file paths");
  assert_true(header_text.find("profile_yaml") == std::string::npos,
              "route catalog projection must not expose profile file contents");
}

}  // namespace

int main() {
  try {
    route_catalog_projection_freezes_current_route_and_candidate_guards();
    route_catalog_projection_can_express_fail_closed_route_state_without_dumping_policy();
    route_catalog_projection_header_stays_summary_only();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiRouteCatalogProjectionTest] FAILED: " << exception.what()
              << '\n';
    return 1;
  }

  return 0;
}