#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "data/ITuiDataSource.h"
#include "model/TuiScreenModel.h"
#include "terminal/FtxuiRendererAdapter.h"
#include "terminal/TuiTerminalCapabilityProbe.h"
#include "view/TuiComposer.h"
#include "view/TuiModelSelector.h"

namespace dasall::tui::app {

struct TuiAppOptions {
  std::string scenario_id = "planning_tools";
  std::optional<std::string> profile_id;
  std::unique_ptr<data::ITuiDataSource> data_source_override;
  std::optional<terminal::TuiTerminalProbeEnvironment> probe_environment;
  std::size_t terminal_width = 0;
  std::size_t terminal_height = 0;
  std::size_t bootstrap_tick_count = 0;
  std::optional<std::string> initial_draft;
  std::optional<data::TuiRoutePreferenceMode> selector_preview_mode;
  bool print_final_screen = true;
  std::ostream* output_stream = nullptr;
};

class TuiApp {
 public:
  TuiApp();
  explicit TuiApp(terminal::TuiTerminalCapabilityProbe probe);

  [[nodiscard]] int run(TuiAppOptions options);

  void dispatch_action(const model::TuiAction& action);
  [[nodiscard]] bool tick();
  [[nodiscard]] int shutdown();

  [[nodiscard]] const model::TuiScreenModel& screen_model() const noexcept;
  [[nodiscard]] const std::vector<std::string>& rendered_frames() const noexcept;
  [[nodiscard]] const std::string& last_rendered_screen() const noexcept;
  [[nodiscard]] const terminal::TuiTerminalCapabilities& terminal_capabilities() const noexcept;
  [[nodiscard]] terminal::TuiStartupMode startup_mode() const noexcept;
  [[nodiscard]] bool session_open() const noexcept;
  [[nodiscard]] bool shutdown_clean() const noexcept;
  [[nodiscard]] std::string_view last_error() const noexcept;

 private:
    void initialize_components(TuiAppOptions& options);
    [[nodiscard]] bool open_session();
  [[nodiscard]] bool load_route_catalog();
  void sync_composer_state();
  void sync_composer_busy_from_status();
  void show_selector_preview(data::TuiRoutePreferenceMode mode);
  void render_current_screen(bool flush_to_output);
    void emit_startup_error() const;
  [[nodiscard]] std::string selector_modal_body(
      const std::vector<view::TuiModelSelectorOption>& options) const;
  [[nodiscard]] std::size_t effective_terminal_width() const noexcept;
  [[nodiscard]] std::size_t effective_terminal_height() const noexcept;
    [[nodiscard]] static std::string format_startup_issue_message(
      const data::TuiDataSourceIssue& issue);
  static std::string startup_mode_to_string(terminal::TuiStartupMode startup_mode);
  static bool status_requires_busy_composer(const data::TuiStatusProjection& status);
  [[nodiscard]] std::string next_request_id(std::string_view prefix);
  [[nodiscard]] std::string next_trace_id(std::string_view prefix);
  void append_issue_banner(const data::TuiDataSourceIssue& issue, std::string title);

  terminal::TuiTerminalCapabilityProbe probe_;
  terminal::FtxuiRendererAdapter renderer_;
  view::TuiComposer composer_;
  view::TuiModelSelector selector_;
  model::TuiScreenModel screen_model_;
  std::unique_ptr<data::ITuiDataSource> data_source_;
  terminal::TuiTerminalCapabilities terminal_capabilities_;
  terminal::TuiStartupMode startup_mode_ = terminal::TuiStartupMode::FailClosed;
  std::optional<std::string> last_event_cursor_;
  std::vector<std::string> rendered_frames_;
  std::string last_rendered_screen_;
  std::string last_error_;
  std::string session_id_;
  std::string scenario_id_;
  std::optional<std::string> profile_id_;
  std::size_t terminal_width_ = 0;
  std::size_t terminal_height_ = 0;
  std::size_t request_sequence_ = 0;
  std::ostream* output_stream_ = nullptr;
  bool session_open_ = false;
  bool shutdown_clean_ = false;
  bool exit_requested_ = false;
};

}  // namespace dasall::tui::app