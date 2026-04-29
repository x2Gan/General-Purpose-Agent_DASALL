#include "AccessGateway.h"

#include <string>
#include <utility>

namespace dasall::access {

AccessGateway::AccessGateway(SubmitPipeline submit_pipeline,
                             PublishBackend publish_backend,
                             ShutdownObserver shutdown_observer)
    : submit_pipeline_(std::move(submit_pipeline)),
      publish_backend_(std::move(publish_backend)),
      shutdown_observer_(std::move(shutdown_observer)) {}

bool AccessGateway::init() {
  AccessGatewayState expected = AccessGatewayState::Uninitialized;
  if (!state_.compare_exchange_strong(expected, AccessGatewayState::Initializing)) {
    return expected == AccessGatewayState::Ready;
  }

  state_.store(AccessGatewayState::Ready);
  return true;
}

RuntimeDispatchResult AccessGateway::submit(const InboundPacket& packet) {
  const auto current = state();
  if (current != AccessGatewayState::Ready) {
    return make_shutting_down_result();
  }

  InflightGuard guard(this);
  return run_submit_pipeline(packet);
}

bool AccessGateway::publish_result(const PublishEnvelope& envelope) {
  const auto current = state();
  if (current != AccessGatewayState::Ready && current != AccessGatewayState::Draining) {
    return false;
  }

  if (!publish_backend_) {
    return false;
  }

  return publish_backend_(envelope);
}

AccessGatewayState AccessGateway::state() const {
  return state_.load();
}

bool AccessGateway::is_ready() const {
  return state() == AccessGatewayState::Ready;
}

void AccessGateway::shutdown(std::chrono::milliseconds drain_timeout) {
  const auto current = state();
  if (current == AccessGatewayState::ShutDown) {
    return;
  }

  state_.store(AccessGatewayState::Draining);

  std::unique_lock<std::mutex> lock(inflight_mutex_);
  const bool drained = inflight_drained_cv_.wait_for(lock, drain_timeout, [this]() {
    return inflight_requests_ == 0;
  });

  const auto abandoned_requests = inflight_requests_;
  lock.unlock();

  if (!drained && abandoned_requests > 0 && shutdown_observer_) {
    shutdown_observer_(abandoned_requests);
  }

  state_.store(AccessGatewayState::ShutDown);
}

AccessGateway::InflightGuard::InflightGuard(AccessGateway* gateway) : gateway_(gateway) {
  std::lock_guard<std::mutex> lock(gateway_->inflight_mutex_);
  ++gateway_->inflight_requests_;
}

AccessGateway::InflightGuard::~InflightGuard() {
  std::lock_guard<std::mutex> lock(gateway_->inflight_mutex_);
  if (gateway_->inflight_requests_ > 0) {
    --gateway_->inflight_requests_;
  }
  if (gateway_->inflight_requests_ == 0) {
    gateway_->inflight_drained_cv_.notify_all();
  }
}

RuntimeDispatchResult AccessGateway::run_submit_pipeline(const InboundPacket& packet) {
  if (!submit_pipeline_) {
    RuntimeDispatchResult rejected;
    rejected.disposition = AccessDisposition::Rejected;
    rejected.error_ref = std::string("submit_pipeline_not_configured");
    rejected.response_context["error_code"] =
        std::to_string(static_cast<int>(AccessErrorCode::ValidationRejected));
    return rejected;
  }

  return submit_pipeline_(packet);
}

RuntimeDispatchResult AccessGateway::make_shutting_down_result() {
  RuntimeDispatchResult rejected;
  rejected.disposition = AccessDisposition::Rejected;
  rejected.error_ref = std::string("gateway_not_ready_or_shutting_down");
  rejected.response_context["error_code"] =
      std::to_string(static_cast<int>(AccessErrorCode::ShuttingDown));
  return rejected;
}

}  // namespace dasall::access
