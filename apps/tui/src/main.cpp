#include <array>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

#include "app/TuiApp.h"
#if DASALL_TUI_FORMAL_ENTRYPOINT
#include "data/DaemonTuiDataSource.h"
#else
#include "data/FakeTuiDataSource.h"
#endif

namespace {

constexpr std::array<std::string_view, 10> kLegacyStructuredCommands = {
    "help",      "version", "config",    "ping",  "readiness",
    "knowledge", "run",     "status",    "cancel", "diag"};

[[nodiscard]] bool is_legacy_structured_command(const std::string_view arg) {
  for (const std::string_view command : kLegacyStructuredCommands) {
    if (command == arg) {
      return true;
    }
  }

  return false;
}

void print_usage(std::ostream& output) {
  output << "Usage: dasall\n"
         << "  bare dasall starts the interactive terminal UI.\n"
         << "  Use dasall-cli for config, ping, readiness, knowledge, run, status, cancel, and diag.\n";
}

[[nodiscard]] std::optional<int> maybe_handle_command_line(int argc, char* argv[]) {
  if (argc <= 1) {
    return std::nullopt;
  }

  const std::string_view first_argument(argv[1]);
  if (first_argument == "--help" || first_argument == "-h") {
    print_usage(std::cout);
    return 0;
  }

  if (is_legacy_structured_command(first_argument)) {
    std::cerr << "bare 'dasall " << first_argument
              << "' is no longer supported. Use 'dasall-cli " << first_argument
              << "' for structured control-plane tasks.\n";
    return 1;
  }

  std::cerr << "invalid arguments for dasall TUI. Use 'dasall --help' for usage and 'dasall-cli' for structured control-plane tasks.\n";
  return 1;
}

[[nodiscard]] dasall::tui::app::TuiAppOptions make_default_options() {
  dasall::tui::app::TuiAppOptions options;
#if DASALL_TUI_FORMAL_ENTRYPOINT
  options.scenario_id = "daemon";
  options.data_source_override = std::make_unique<dasall::tui::data::DaemonTuiDataSource>(
      dasall::tui::data::resolve_daemon_tui_controller_options_from_environment());
#else
  options.scenario_id = "planning_tools";
  options.data_source_override = std::make_unique<dasall::tui::data::FakeTuiDataSource>(
      options.scenario_id);
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

int main(int argc, char* argv[]) {
  if (const std::optional<int> exit_code = maybe_handle_command_line(argc, argv);
      exit_code.has_value()) {
    return *exit_code;
  }

  dasall::tui::app::TuiApp app;
  dasall::tui::app::TuiAppOptions options = make_default_options();
  return app.run(std::move(options));
}