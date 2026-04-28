#include "DaemonLifecycleController.h"

namespace dasall::apps::daemon {

bool DaemonLifecycleController::start() {
  std::scoped_lock lock(mutex_);
  if (state_ != DaemonLifecycleState::Stopped) {
    return false;
  }

  state_ = DaemonLifecycleState::Bootstrapping;
  return true;
}

bool DaemonLifecycleController::mark_binding() {
  std::scoped_lock lock(mutex_);
  if (state_ != DaemonLifecycleState::Bootstrapping) {
    return false;
  }

  state_ = DaemonLifecycleState::Binding;
  return true;
}

bool DaemonLifecycleController::mark_ready() {
  std::scoped_lock lock(mutex_);
  if (state_ != DaemonLifecycleState::Binding) {
    return false;
  }

  state_ = DaemonLifecycleState::Ready;
  return true;
}

bool DaemonLifecycleController::mark_failed() {
  std::scoped_lock lock(mutex_);
  if (state_ == DaemonLifecycleState::Stopped ||
      state_ == DaemonLifecycleState::Failed) {
    return false;
  }

  state_ = DaemonLifecycleState::Failed;
  return true;
}

bool DaemonLifecycleController::begin_request() {
  std::scoped_lock lock(mutex_);
  if (state_ != DaemonLifecycleState::Ready) {
    return false;
  }

  ++inflight_requests_;
  return true;
}

void DaemonLifecycleController::finish_request() {
  std::scoped_lock lock(mutex_);
  if (inflight_requests_ == 0) {
    return;
  }

  --inflight_requests_;
  if (inflight_requests_ == 0) {
    inflight_drained_.notify_all();
  }
}

DaemonShutdownResult DaemonLifecycleController::shutdown(
    std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);

  if (state_ == DaemonLifecycleState::Stopped) {
    return DaemonShutdownResult{.drained = true};
  }

  if (state_ != DaemonLifecycleState::Failed) {
    state_ = DaemonLifecycleState::Draining;
  }

  if (inflight_requests_ == 0) {
    state_ = DaemonLifecycleState::Stopped;
    return DaemonShutdownResult{.drained = true};
  }

  const bool drained = inflight_drained_.wait_for(
      lock, timeout, [this]() { return inflight_requests_ == 0; });
  if (drained) {
    state_ = DaemonLifecycleState::Stopped;
    return DaemonShutdownResult{.drained = true};
  }

  const auto abandoned_requests = inflight_requests_;
  state_ = DaemonLifecycleState::Stopped;
  return DaemonShutdownResult{.drained = false,
                              .timed_out = true,
                              .abandoned_requests = abandoned_requests};
}

DaemonLifecycleState DaemonLifecycleController::state() const {
  std::scoped_lock lock(mutex_);
  return state_;
}

DaemonLifecycleObservation DaemonLifecycleController::observation() const {
  std::scoped_lock lock(mutex_);
  switch (state_) {
    case DaemonLifecycleState::Bootstrapping:
    case DaemonLifecycleState::Binding:
      return DaemonLifecycleObservation::Starting;
    case DaemonLifecycleState::Ready:
      return DaemonLifecycleObservation::Ready;
    case DaemonLifecycleState::Draining:
      return DaemonLifecycleObservation::Stopping;
    case DaemonLifecycleState::Failed:
      return DaemonLifecycleObservation::NotReady;
    case DaemonLifecycleState::Stopped:
    default:
      return DaemonLifecycleObservation::Stopped;
  }
}

bool DaemonLifecycleController::allows_new_requests() const {
  std::scoped_lock lock(mutex_);
  return state_ == DaemonLifecycleState::Ready;
}

std::uint32_t DaemonLifecycleController::inflight_requests() const {
  std::scoped_lock lock(mutex_);
  return inflight_requests_;
}

}  // namespace dasall::apps::daemon