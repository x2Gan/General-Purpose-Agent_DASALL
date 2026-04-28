#include "DaemonSignalHandler.h"

namespace dasall::apps::daemon {

DaemonSignalHandler* DaemonSignalHandler::active_instance_ = nullptr;

DaemonSignalHandler::~DaemonSignalHandler() {
  uninstall_handlers();
}

bool DaemonSignalHandler::install_handlers() {
  if (active_instance_ != nullptr && active_instance_ != this) {
    return false;
  }

  previous_sigterm_ = std::signal(SIGTERM, &DaemonSignalHandler::dispatch_signal);
  if (previous_sigterm_ == SIG_ERR) {
    previous_sigterm_ = SIG_DFL;
    return false;
  }

  previous_sigint_ = std::signal(SIGINT, &DaemonSignalHandler::dispatch_signal);
  if (previous_sigint_ == SIG_ERR) {
    std::signal(SIGTERM, previous_sigterm_);
    previous_sigterm_ = SIG_DFL;
    previous_sigint_ = SIG_DFL;
    return false;
  }

  previous_sighup_ = std::signal(SIGHUP, &DaemonSignalHandler::dispatch_signal);
  if (previous_sighup_ == SIG_ERR) {
    std::signal(SIGTERM, previous_sigterm_);
    std::signal(SIGINT, previous_sigint_);
    previous_sigterm_ = SIG_DFL;
    previous_sigint_ = SIG_DFL;
    previous_sighup_ = SIG_DFL;
    return false;
  }

  clear_requests();
  active_instance_ = this;
  installed_ = true;
  return true;
}

void DaemonSignalHandler::request_shutdown(int signal) {
  shutdown_requested_ = 1;
  last_signal_ = signal;
}

void DaemonSignalHandler::request_reload(int signal) {
  reload_requested_ = 1;
  last_signal_ = signal;
}

bool DaemonSignalHandler::shutdown_requested() const {
  return shutdown_requested_ != 0;
}

bool DaemonSignalHandler::reload_requested() const {
  return reload_requested_ != 0;
}

int DaemonSignalHandler::last_signal() const {
  return static_cast<int>(last_signal_);
}

void DaemonSignalHandler::clear_requests() {
  shutdown_requested_ = 0;
  reload_requested_ = 0;
  last_signal_ = 0;
}

void DaemonSignalHandler::dispatch_signal(int signal) {
  if (active_instance_ != nullptr) {
    active_instance_->handle_signal(signal);
  }
}

void DaemonSignalHandler::handle_signal(int signal) {
  switch (signal) {
    case SIGTERM:
    case SIGINT:
      request_shutdown(signal);
      break;
    case SIGHUP:
      request_reload(signal);
      break;
    default:
      last_signal_ = signal;
      break;
  }
}

void DaemonSignalHandler::uninstall_handlers() {
  if (!installed_ || active_instance_ != this) {
    return;
  }

  std::signal(SIGTERM, previous_sigterm_);
  std::signal(SIGINT, previous_sigint_);
  std::signal(SIGHUP, previous_sighup_);
  active_instance_ = nullptr;
  installed_ = false;
}

}  // namespace dasall::apps::daemon