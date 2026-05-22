#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "data/FakeScenarioCatalog.h"
#include "support/TestAssertions.h"
#include "view/TuiModelSelector.h"

#ifndef DASALL_TUI_MODEL_SELECTOR_HEADER
#define DASALL_TUI_MODEL_SELECTOR_HEADER \
  "/home/gangan/DASALL/apps/tui/src/view/TuiModelSelector.h"
#endif

#ifndef DASALL_TUI_MODEL_SELECTOR_IMPL
#define DASALL_TUI_MODEL_SELECTOR_IMPL \
  "/home/gangan/DASALL/apps/tui/src/view/TuiModelSelector.cpp"
#endif

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeScenarioCatalog;
using dasall::tui::data::NextTurnPreference;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::view::TuiModelSelector;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string optional_string(const std::optional<std::string>& value) {
  return value.value_or("<none>");
}

void selector_tracks_fake_draft_apply_and_cancel() {
  const auto scenario = FakeScenarioCatalog::load("route_switch");
  assert_true(scenario.ok(),
              "route_switch should provide the fake selector scenario for TUI-TODO-015");

  TuiModelSelector selector(scenario.scenario->route_catalog);
  assert_equal(static_cast<int>(TuiRoutePreferenceMode::PreferDepth),
               static_cast<int>(selector.draft().mode),
               "route_switch should seed the selector from the fake next-turn draft");
  assert_equal("deep",
               optional_string(selector.draft().preferred_depth_tier),
               "route_switch should expose the preselected fake depth preference");
  assert_equal("prefer depth",
               selector.draft().user_visible_summary,
               "route_switch should keep the fake selector summary visible before edits");

  const auto initial_options = selector.open_selector();
  assert_true(selector.is_open(),
              "open_selector should move the selector into an open local modal state");
  assert_equal(static_cast<int>(TuiRoutePreferenceMode::PreferDepth),
               static_cast<int>(selector.open_mode()),
               "opening without an explicit mode should keep the existing draft mode");
  assert_equal(3,
               static_cast<int>(initial_options.size()),
               "prefer-depth mode should expose the fake route depth tiers");

  assert_true(selector.choose_depth_tier("fast"),
              "fake selector should allow switching to a selectable depth tier");
  const NextTurnPreference fast_preference = selector.apply_preference();
  assert_true(!selector.is_open(),
              "apply_preference should close the selector after committing the draft");
  assert_equal(static_cast<int>(TuiRoutePreferenceMode::PreferDepth),
               static_cast<int>(fast_preference.mode),
               "choosing a depth tier should keep the selector in PreferDepth mode");
  assert_equal("fast",
               optional_string(fast_preference.preferred_depth_tier),
               "choosing a depth tier should update the next-turn depth preference");
  assert_true(!fast_preference.pinned_provider_id.has_value() &&
                  !fast_preference.pinned_model_id.has_value(),
              "prefer-depth drafts should not pin a concrete provider or model");
  assert_equal("prefer depth: fast",
               fast_preference.user_visible_summary,
               "applied depth preferences should expose a user-visible fake summary");
  assert_equal("tui_model_selector",
               fast_preference.source,
               "applied fake selector drafts should record the selector source");
  assert_true(fast_preference.applies_to_next_turn_only,
              "fake selector drafts must remain next-turn-only");

  static_cast<void>(selector.open_selector(TuiRoutePreferenceMode::PinModel));
  assert_true(selector.choose_model("provider-openai", "gpt-4.1-mini"),
              "fake selector should allow pinning a selectable fake route");
  selector.cancel_preference();
  assert_true(!selector.is_open(),
              "cancel_preference should close the selector without committing the new draft");
  assert_equal(static_cast<int>(TuiRoutePreferenceMode::PreferDepth),
               static_cast<int>(selector.draft().mode),
               "cancelling should restore the previously committed depth preference");
  assert_equal("fast",
               optional_string(selector.draft().preferred_depth_tier),
               "cancelling should keep the previously committed fake depth tier");

  static_cast<void>(selector.open_selector(TuiRoutePreferenceMode::PinModel));
  assert_true(selector.choose_model("provider-openai", "gpt-4.1-mini"),
              "the same selectable fake route should still be pinnable after a cancel");
  const NextTurnPreference pinned_preference = selector.apply_preference();
  assert_equal(static_cast<int>(TuiRoutePreferenceMode::PinModel),
               static_cast<int>(pinned_preference.mode),
               "pinning should switch the selector into PinModel mode");
  assert_equal("provider-openai",
               optional_string(pinned_preference.pinned_provider_id),
               "pinning should keep the chosen provider id in the next-turn draft");
  assert_equal("gpt-4.1-mini",
               optional_string(pinned_preference.pinned_model_id),
               "pinning should keep the chosen model id in the next-turn draft");
  assert_true(!pinned_preference.preferred_depth_tier.has_value(),
              "pinning should clear any advisory depth preference from the draft");
  assert_equal("pin model: provider-openai/gpt-4.1-mini",
               pinned_preference.user_visible_summary,
               "pinning should expose a user-visible fake model summary");

  static_cast<void>(selector.open_selector(TuiRoutePreferenceMode::Auto));
  const NextTurnPreference auto_preference = selector.apply_preference();
  assert_equal(static_cast<int>(TuiRoutePreferenceMode::Auto),
               static_cast<int>(auto_preference.mode),
               "auto mode should clear the selector back to profile-driven routing");
  assert_true(!auto_preference.preferred_depth_tier.has_value() &&
                  !auto_preference.pinned_provider_id.has_value() &&
                  !auto_preference.pinned_model_id.has_value(),
              "auto mode should clear all explicit next-turn route hints");
  assert_equal("auto",
               auto_preference.user_visible_summary,
               "auto mode should expose a stable user-visible summary");
}

void selector_files_avoid_owner_private_or_renderer_dependencies() {
  const std::string header_text =
      read_text_file(std::filesystem::path{DASALL_TUI_MODEL_SELECTOR_HEADER});
  const std::string impl_text =
      read_text_file(std::filesystem::path{DASALL_TUI_MODEL_SELECTOR_IMPL});

  for (const std::string* file_text : {&header_text, &impl_text}) {
    assert_true(file_text->find("access/") == std::string::npos,
                "selector files should not include access private headers");
    assert_true(file_text->find("runtime/") == std::string::npos,
                "selector files should not include runtime private headers");
    assert_true(file_text->find("llm/") == std::string::npos,
                "selector files should not include llm private headers");
    assert_true(file_text->find("profiles/") == std::string::npos,
                "selector files should not include profile private headers");
    assert_true(file_text->find("ftxui") == std::string::npos,
                "selector files should stay independent from the renderer implementation");
    assert_true(file_text->find("iostream") == std::string::npos,
                "selector production files should not perform stream I/O");
    assert_true(file_text->find("fstream") == std::string::npos,
                "selector production files should not depend on file I/O");
    assert_true(file_text->find("filesystem") == std::string::npos,
                "selector production files should not depend on filesystem APIs");
    assert_true(file_text->find("chrono") == std::string::npos,
                "selector production files should not read clock state directly");
  }
}

}  // namespace

int main() {
  try {
    selector_tracks_fake_draft_apply_and_cancel();
    selector_files_avoid_owner_private_or_renderer_dependencies();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiModelSelectorTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}