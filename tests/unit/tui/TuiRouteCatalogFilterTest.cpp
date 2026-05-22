#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "data/FakeScenarioCatalog.h"
#include "support/TestAssertions.h"
#include "view/TuiModelSelector.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tui::data::FakeScenarioCatalog;
using dasall::tui::data::TuiRoutePreferenceMode;
using dasall::tui::view::TuiModelSelector;
using dasall::tui::view::TuiModelSelectorOption;

[[nodiscard]] const TuiModelSelectorOption* find_option(
    const std::vector<TuiModelSelectorOption>& options,
    const std::string& provider_id,
    const std::string& model_id) {
  for (const TuiModelSelectorOption& option : options) {
    if (option.provider_id == provider_id && option.model_id == model_id) {
      return &option;
    }
  }

  return nullptr;
}

void selector_filters_depths_and_pin_candidates_from_fake_route_catalog() {
  const auto scenario = FakeScenarioCatalog::load("route_switch");
  assert_true(scenario.ok(),
              "route_switch should provide the fake selector route catalog scenario");

  TuiModelSelector selector(scenario.scenario->route_catalog);

  const auto depth_options = selector.open_selector(TuiRoutePreferenceMode::PreferDepth);
  assert_equal(3,
               static_cast<int>(depth_options.size()),
               "prefer-depth mode should collapse fake candidates into unique depth tiers");
  assert_equal("balanced",
               depth_options[0].display_label,
               "depth options should preserve the fake route order for stable selector rendering");
  assert_true(depth_options[0].selectable && !depth_options[0].selected,
              "the current balanced route should remain selectable but not preselected");
  assert_equal("fast",
               depth_options[1].display_label,
               "the fast tier should remain visible as the second selectable fake option");
  assert_true(depth_options[1].selectable && !depth_options[1].selected,
              "the fast tier should remain selectable before the user changes the draft");
  assert_equal("deep",
               depth_options[2].display_label,
               "the deep tier should stay visible even when all fake routes are disabled");
  assert_true(!depth_options[2].selectable && depth_options[2].selected,
              "the preselected deep fake draft should surface as disabled when the catalog disallows it");
  assert_equal("credentials missing, verification pending, allowlist blocked",
               selector.render_disabled_reason(depth_options[2].disabled_reasons),
               "disabled depth tiers should expose merged fake disabled reasons");

  assert_true(!selector.choose_depth_tier("deep"),
              "disabled depth tiers should fail closed instead of silently applying");
  assert_true(selector.choose_depth_tier("fast"),
              "selectable fake depth tiers should apply locally");
  const auto refreshed_depth_options = selector.filtered_options();
  assert_true(refreshed_depth_options[1].selected,
              "after a local depth change the refreshed selector should mark the new tier selected");

  const auto pin_options = selector.open_selector(TuiRoutePreferenceMode::PinModel);
  assert_equal(4,
               static_cast<int>(pin_options.size()),
               "pin-model mode should expose all fake route catalog entries, including disabled ones");

  const TuiModelSelectorOption* local_option =
      find_option(pin_options, "provider-local", "deep-reasoner");
  assert_true(local_option != nullptr && !local_option->selectable,
              "disabled local routes should still remain visible in the fake pin-model list");
  assert_equal("verification pending, allowlist blocked",
               selector.render_disabled_reason(local_option->disabled_reasons),
               "disabled local fake routes should expose stable disabled reason text");

  const TuiModelSelectorOption* anthropic_option =
      find_option(pin_options, "provider-anthropic", "claude-sonnet");
  assert_true(anthropic_option != nullptr && !anthropic_option->selectable,
              "credential-blocked fake routes should remain visible but disabled");
  assert_equal("credentials missing",
               selector.render_disabled_reason(anthropic_option->disabled_reasons),
               "credential-blocked fake routes should keep their disabled reason text");

  assert_true(!selector.choose_model("provider-local", "deep-reasoner"),
              "disabled local fake routes should fail closed when the user tries to pin them");
  assert_true(!selector.choose_model("provider-anthropic", "claude-sonnet"),
              "credential-blocked fake routes should fail closed when the user tries to pin them");
  assert_true(selector.choose_model("provider-openai", "gpt-4.1-mini"),
              "selectable fake routes should remain pinnable in pin-model mode");
  const auto refreshed_pin_options = selector.filtered_options();
  const TuiModelSelectorOption* selected_option =
      find_option(refreshed_pin_options, "provider-openai", "gpt-4.1-mini");
  assert_true(selected_option != nullptr && selected_option->selected,
              "after choosing a pin candidate the refreshed selector should mark it selected");
}

}  // namespace

int main() {
  try {
    selector_filters_depths_and_pin_candidates_from_fake_route_catalog();
  } catch (const std::exception& exception) {
    std::cerr << "[TuiRouteCatalogFilterTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}