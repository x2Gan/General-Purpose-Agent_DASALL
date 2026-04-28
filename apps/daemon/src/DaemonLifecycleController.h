#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace dasall::apps::daemon {

enum class DaemonLifecycleState {
  Stopped = 0,
  Bootstrapping,
  Binding,
  Ready,
  Draining,
  Failed,
};

enum class DaemonLifecycleObservation {
  Starting = 0,
  Ready,
  Stopping,
  Stopped,
  NotReady,
};

struct DaemonShutdownResult {
  bool drained = false;
  bool timed_out = false;
  std::uint32_t abandoned_requests = 0;
};

class DaemonLifecycleController {
 public:
  [[nodiscard]] bool start();
  [[nodiscard]] bool mark_binding();
  [[nodiscard]] bool mark_ready();
  [[nodiscard]] bool mark_failed();

  [[nodiscard]] bool begin_request();
  void finish_request();

  [[nodiscard]] DaemonShutdownResult shutdown(std::chrono::milliseconds timeout);

  [[nodiscard]] DaemonLifecycleState state() const;
  [[nodiscard]] DaemonLifecycleObservation observation() const;
  [[nodiscard]] bool allows_new_requests() const;
  [[nodiscard]] std::uint32_t inflight_requests() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable inflight_drained_;
  DaemonLifecycleState state_ = DaemonLifecycleState::Stopped;
  std::uint32_t inflight_requests_ = 0;
};

}  // namespace dasall::apps::daemon