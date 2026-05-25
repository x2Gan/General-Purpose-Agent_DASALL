#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "app/TuiApp.h"
#include "model/TuiAction.h"
#if DASALL_TUI_FORMAL_ENTRYPOINT
#include "data/DaemonTuiDataSource.h"
#else
#include "data/FakeTuiDataSource.h"
#endif

namespace {

constexpr std::array<std::string_view, 10> kLegacyStructuredCommands = {
    "help",      "version", "config",    "ping",  "readiness",
    "knowledge", "run",     "status",    "cancel", "diag"};
#if DASALL_TUI_FORMAL_ENTRYPOINT
constexpr std::string_view kScriptedSmokeModeEnv = "DASALL_TUI_SCRIPTED_SMOKE";
constexpr std::string_view kScriptedSmokePromptEnv =
  "DASALL_TUI_SCRIPTED_SMOKE_PROMPT";
constexpr std::string_view kScriptedSmokeProfileEnv =
  "DASALL_TUI_SCRIPTED_SMOKE_PROFILE_ID";
constexpr std::string_view kDaemonRoundtripSmokeMode = "daemon_roundtrip";
constexpr std::string_view kJsonHexDigits = "0123456789ABCDEF";

[[nodiscard]] dasall::tui::app::TuiAppOptions make_default_options();
#endif

[[nodiscard]] bool is_legacy_structured_command(const std::string_view arg) {
  for (const std::string_view command : kLegacyStructuredCommands) {
    if (command == arg) {
      return true;
    }
  }

  return false;
}

#if DASALL_TUI_FORMAL_ENTRYPOINT
[[nodiscard]] std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20U) {
          escaped += "\\u00";
          escaped.push_back(kJsonHexDigits[byte >> 4U]);
          escaped.push_back(kJsonHexDigits[byte & 0x0FU]);
        } else {
          escaped.push_back(ch);
        }
        break;
    }
  }

  return escaped;
}

[[nodiscard]] const char* getenv_nonempty(const char* name) {
  const char* value = std::getenv(name);
  return (value != nullptr && *value != '\0') ? value : nullptr;
}

[[nodiscard]] dasall::tui::terminal::TuiTerminalProbeEnvironment
make_scripted_smoke_probe_environment() {
  dasall::tui::terminal::TuiTerminalProbeEnvironment environment;
  environment.stdin_is_tty = true;
  environment.stdout_is_tty = true;
  environment.stderr_is_tty = true;
  environment.term = "xterm-256color";
  environment.locale = "en_US.UTF-8";
  environment.columns = 120;
  environment.rows = 36;
  environment.utf8_enabled = true;
  environment.bracketed_paste_supported = true;
  environment.resize_events_supported = true;
  environment.external_editor_available = true;
  return environment;
}

[[nodiscard]] dasall::tui::model::TuiAction make_turn_submit_action() {
  dasall::tui::model::TuiAction action;
  action.type = dasall::tui::model::TuiActionType::TurnSubmitRequested;
  action.debug_reason = "scripted_smoke_submit";
  return action;
}

[[nodiscard]] std::string build_scripted_smoke_json(
    const dasall::tui::app::TuiApp& app,
    std::string_view mode) {
  const auto& screen_model = app.screen_model();
  const std::string latest_transcript_content =
      screen_model.transcript.empty() ? std::string{} : screen_model.transcript.back().content;
  const std::string latest_banner_title =
      screen_model.banners.empty() ? std::string{} : screen_model.banners.back().title;
  const std::string latest_banner_message =
      screen_model.banners.empty() ? std::string{} : screen_model.banners.back().message;
  const bool session_closed_cleanly = app.shutdown_clean() && !app.session_open();
  const bool rendered_screen_contains_receipt =
      app.last_rendered_screen().find(latest_transcript_content) != std::string::npos;
  const bool rendered_screen_contains_route =
      app.last_rendered_screen().find(screen_model.route.current_provider_id) != std::string::npos &&
      app.last_rendered_screen().find(screen_model.route.current_model_id) != std::string::npos;

  std::ostringstream json;
  json << '{'
      << "\"mode\":\"" << json_escape(mode) << "\","
       << "\"shutdown_clean\":" << (app.shutdown_clean() ? "true" : "false") << ','
       << "\"session_closed_cleanly\":"
       << (session_closed_cleanly ? "true" : "false") << ','
      << "\"profile_id\":\"" << json_escape(screen_model.session.profile_id) << "\","
      << "\"startup_mode\":\"" << json_escape(screen_model.session.startup_mode) << "\","
       << "\"daemon_readiness\":\""
      << json_escape(screen_model.session.daemon_readiness) << "\","
       << "\"current_provider_id\":\""
      << json_escape(screen_model.route.current_provider_id) << "\","
       << "\"current_model_id\":\""
      << json_escape(screen_model.route.current_model_id) << "\","
      << "\"status_stage\":\"" << json_escape(screen_model.status.stage) << "\","
       << "\"status_current_tool\":\""
      << json_escape(screen_model.status.current_tool) << "\","
       << "\"transcript_count\":" << screen_model.transcript.size() << ','
       << "\"latest_transcript_content\":\""
      << json_escape(latest_transcript_content) << "\","
       << "\"latest_banner_title\":\""
      << json_escape(latest_banner_title) << "\","
       << "\"latest_banner_message\":\""
      << json_escape(latest_banner_message) << "\","
       << "\"rendered_screen_contains_receipt\":"
       << (rendered_screen_contains_receipt ? "true" : "false") << ','
       << "\"rendered_screen_contains_route\":"
       << (rendered_screen_contains_route ? "true" : "false") << ','
       << "\"non_extrapolation\":["
      << "\"This smoke only proves installed bare dasall can drive one scripted daemon-backed roundtrip.\","
       << "\"It does not imply installed release closure, purity closure, or qemu isolation evidence.\""
       << "]}";
  return json.str();
}
#endif

void print_usage(std::ostream& output) {
  output << "Usage: dasall\n"
         << "  bare dasall starts the interactive terminal UI.\n"
         << "  Use dasall-cli for config, ping, readiness, knowledge, run, status, cancel, and diag.\n";
}

#if DASALL_TUI_FORMAL_ENTRYPOINT
[[nodiscard]] int run_scripted_smoke(std::string_view mode) {
  if (mode != kDaemonRoundtripSmokeMode) {
    std::cerr << "unknown TUI scripted smoke mode: " << mode << '\n';
    return 1;
  }

  dasall::tui::app::TuiApp app;
  dasall::tui::app::TuiAppOptions options = make_default_options();
  options.probe_environment = make_scripted_smoke_probe_environment();
  options.terminal_width = 120;
  options.terminal_height = 36;
  options.print_final_screen = false;
  options.output_stream = nullptr;
  options.post_action_tick_count = 1;
  options.initial_draft = std::string(getenv_nonempty(kScriptedSmokePromptEnv.data()) != nullptr
                                          ? getenv_nonempty(kScriptedSmokePromptEnv.data())
                                          : "installed daemon-backed tui smoke");
  if (const char* profile_id = getenv_nonempty(kScriptedSmokeProfileEnv.data());
      profile_id != nullptr) {
    options.profile_id = std::string(profile_id);
  }
  options.scripted_actions.push_back(make_turn_submit_action());

  const int exit_code = app.run(std::move(options));
  if (exit_code != 0) {
    if (!app.last_error().empty()) {
      std::cerr << app.last_error() << '\n';
    }
    return exit_code;
  }

  std::cout << build_scripted_smoke_json(app, mode) << '\n';
  return 0;
}
#endif

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
#if DASALL_TUI_FORMAL_ENTRYPOINT
  if (const char* scripted_smoke_mode = getenv_nonempty(kScriptedSmokeModeEnv.data());
      scripted_smoke_mode != nullptr) {
    return run_scripted_smoke(scripted_smoke_mode);
  }
#endif

  if (const std::optional<int> exit_code = maybe_handle_command_line(argc, argv);
      exit_code.has_value()) {
    return *exit_code;
  }

  dasall::tui::app::TuiApp app;
  dasall::tui::app::TuiAppOptions options = make_default_options();
#if DASALL_TUI_FORMAL_ENTRYPOINT
  options.interactive_session = true;
#endif
  return app.run(std::move(options));
}