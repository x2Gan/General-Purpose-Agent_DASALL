#include "data/DaemonTuiDataSource.h"

#include <cstdlib>
#include <utility>

namespace dasall::tui::data {

ipc::TuiIpcControllerOptions
resolve_daemon_tui_controller_options_from_environment() {
  ipc::TuiIpcControllerOptions options;
  const char* const value = std::getenv(kTuiDaemonSocketOverrideEnv.data());
  if (value != nullptr && value[0] != '\0') {
    options.socket_path = value;
  }
  return options;
}

DaemonTuiDataSource::DaemonTuiDataSource() = default;

DaemonTuiDataSource::DaemonTuiDataSource(ipc::TuiIpcControllerOptions options)
    : controller_(std::move(options)) {}

DaemonTuiDataSource::DaemonTuiDataSource(ipc::TuiIpcController controller)
    : controller_(std::move(controller)) {}

TuiOpenSessionResult DaemonTuiDataSource::open_session(
    const TuiOpenSessionRequest& request) {
  return controller_.open_session(request);
}

TuiSubmitTurnResult DaemonTuiDataSource::submit_turn(
    const TuiSubmitTurnRequest& request) {
  return controller_.submit_turn(request);
}

TuiPollEventsResult DaemonTuiDataSource::poll_events(
    const TuiPollEventsRequest& request) {
  return controller_.poll_events(request);
}

TuiRouteCatalogResult DaemonTuiDataSource::route_catalog(
    const TuiRouteCatalogRequest& request) {
  return controller_.query_route_catalog(request);
}

TuiCloseSessionResult DaemonTuiDataSource::close_session(
    const TuiCloseSessionRequest& request) {
  return controller_.close_session(request);
}

}  // namespace dasall::tui::data