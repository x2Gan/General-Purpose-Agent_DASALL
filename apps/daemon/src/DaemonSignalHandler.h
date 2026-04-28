#pragma once

#include <csignal>

namespace dasall::apps::daemon {

class DaemonSignalHandler {
 public:
  using SignalCallback = void (*)(int);

  DaemonSignalHandler() = default;
  ~DaemonSignalHandler();

  DaemonSignalHandler(const DaemonSignalHandler&) = delete;
  DaemonSignalHandler& operator=(const DaemonSignalHandler&) = delete;

  [[nodiscard]] bool install_handlers();

  void request_shutdown(int signal = SIGTERM);
  void request_reload(int signal = SIGHUP);

  [[nodiscard]] bool shutdown_requested() const;
  [[nodiscard]] bool reload_requested() const;
  [[nodiscard]] int last_signal() const;

  void clear_requests();

 private:
  static void dispatch_signal(int signal);
  void handle_signal(int signal);
  void uninstall_handlers();

  bool installed_ = false;
  SignalCallback previous_sigterm_ = SIG_DFL;
  SignalCallback previous_sigint_ = SIG_DFL;
  SignalCallback previous_sighup_ = SIG_DFL;

  volatile std::sig_atomic_t shutdown_requested_ = 0;
  volatile std::sig_atomic_t reload_requested_ = 0;
  volatile std::sig_atomic_t last_signal_ = 0;

  static DaemonSignalHandler* active_instance_;
};

}  // namespace dasall::apps::daemon