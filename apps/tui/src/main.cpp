#include <iostream>
#include <memory>

#include "app/TuiApp.h"
#if DASALL_TUI_FORMAL_ENTRYPOINT
#include "data/DaemonTuiDataSource.h"
#endif

namespace {

[[nodiscard]] dasall::tui::app::TuiAppOptions make_default_options() {
  dasall::tui::app::TuiAppOptions options;
#if DASALL_TUI_FORMAL_ENTRYPOINT
  options.scenario_id = "daemon";
  options.data_source_override =
      std::make_unique<dasall::tui::data::DaemonTuiDataSource>();
#else
  options.scenario_id = "planning_tools";
  options.bootstrap_tick_count = 2;
  options.initial_draft =
      "Hold the current draft while tool.search is running.";
  options.selector_preview_mode =
      dasall::tui::data::TuiRoutePreferenceMode::PinModel;
#endif
  options.output_stream = &std::cout;
  return options;
}

}  // namespace

int main() {
  dasall::tui::app::TuiApp app;
  dasall::tui::app::TuiAppOptions options = make_default_options();
  return app.run(std::move(options));
}