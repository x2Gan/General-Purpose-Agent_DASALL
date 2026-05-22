#include <iostream>

#include "app/TuiApp.h"

int main() {
  dasall::tui::app::TuiApp app;
  dasall::tui::app::TuiAppOptions options;
  options.scenario_id = "planning_tools";
  options.bootstrap_tick_count = 2;
  options.initial_draft =
      "Hold the current draft while tool.search is running.";
  options.selector_preview_mode =
      dasall::tui::data::TuiRoutePreferenceMode::PinModel;
  options.output_stream = &std::cout;
  return app.run(std::move(options));
}