#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <utility>

#include "data/ITuiDataSource.h"
#include "logging/ILogger.h"
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
  bool interactive_session = false;
  std::size_t terminal_width = 0;
  std::size_t terminal_height = 0;
  std::size_t bootstrap_tick_count = 0;
  std::size_t post_action_tick_count = 0;
  std::vector<model::TuiAction> scripted_actions;
  std::optional<std::string> initial_draft;
  std::optional<data::TuiRoutePreferenceMode> selector_preview_mode;
  std::shared_ptr<infra::logging::ILogger> logger;
  bool require_logger = false;
  std::string logger_unavailable_reason;
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
  [[nodiscard]] bool logging_degraded() const noexcept;
  [[nodiscard]] std::size_t logging_failure_count() const noexcept;
  [[nodiscard]] std::string_view logging_last_failure_stage() const noexcept;
  [[nodiscard]] std::string_view last_error() const noexcept;

 private:
  [[nodiscard]] int run_interactive_session();
  void initialize_components(TuiAppOptions& options);
  [[nodiscard]] bool open_session();
  [[nodiscard]] bool load_route_catalog();
  void sync_composer_state();
  void sync_composer_busy_from_status();
  [[nodiscard]] bool advance_interactive_animation();
  void set_processing_indicator();
  void request_interactive_redraw();
  void scroll_transcript_page(int direction);
  void set_composer_text(std::string text,
                         std::optional<std::size_t> cursor_offset,
                         std::string debug_reason);
  void sync_pending_draft(const std::string& draft,
                          std::size_t cursor_offset,
                          std::string debug_reason);
  void clear_composer_draft_preserving_history();
  void restore_composer_draft(std::string text, std::size_t cursor_offset);
  void dispatch_composer_submit();
  void handle_interactive_submit();
  void handle_foreground_session_clear();
  void hide_modal(std::string debug_reason);
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
    void note_logging_failure(std::string_view stage,
                const infra::logging::LogWriteResult& result);
  void log_tui_event(
      infra::logging::LogLevel level,
      std::string_view event_name,
      std::string message,
      std::vector<std::pair<std::string, std::string>> attrs = {});
    void flush_tui_logger();

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
  std::shared_ptr<infra::logging::ILogger> logger_;
  std::string logger_unavailable_reason_;
  std::string logging_last_failure_stage_;
  std::size_t terminal_width_ = 0;
  std::size_t terminal_height_ = 0;
  std::size_t request_sequence_ = 0;
  std::size_t logging_failure_count_ = 0;
  std::size_t animation_cursor_tick_ = 0;
  std::size_t animation_spinner_index_ = 0;
  std::ostream* output_stream_ = nullptr;
  std::function<void()> interactive_redraw_hook_;
  std::string shutdown_close_reason_ = "prototype_round_complete";
  bool animation_cursor_visible_ = true;
  bool require_logger_ = false;
  bool logging_degraded_ = false;
  bool logging_degraded_banner_emitted_ = false;
  bool session_open_ = false;
  bool shutdown_clean_ = false;
  bool exit_requested_ = false;
};

}  // namespace dasall::tui::app