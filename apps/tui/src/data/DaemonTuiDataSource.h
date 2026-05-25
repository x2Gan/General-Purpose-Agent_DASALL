#pragma once

#include "data/ITuiDataSource.h"
#include "ipc/TuiIpcController.h"

namespace dasall::tui::data {

inline constexpr std::string_view kTuiDaemonSocketOverrideEnv =
  "DASALL_TUI_DAEMON_SOCKET";

[[nodiscard]] ipc::TuiIpcControllerOptions
resolve_daemon_tui_controller_options_from_environment();

class DaemonTuiDataSource final : public ITuiDataSource {
 public:
  DaemonTuiDataSource();
  explicit DaemonTuiDataSource(ipc::TuiIpcControllerOptions options);
  explicit DaemonTuiDataSource(ipc::TuiIpcController controller);

  TuiOpenSessionResult open_session(const TuiOpenSessionRequest& request) override;
  TuiSubmitTurnResult submit_turn(const TuiSubmitTurnRequest& request) override;
  TuiPollEventsResult poll_events(const TuiPollEventsRequest& request) override;
  TuiRouteCatalogResult route_catalog(const TuiRouteCatalogRequest& request) override;
  TuiCloseSessionResult close_session(const TuiCloseSessionRequest& request) override;

 private:
  ipc::TuiIpcController controller_;
};

}  // namespace dasall::tui::data