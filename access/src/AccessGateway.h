#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>

#include "AccessErrors.h"
#include "IAccessGateway.h"

namespace dasall::access {

class AccessGateway final : public IAccessGateway {
 public:
  using SubmitPipeline = std::function<RuntimeDispatchResult(const InboundPacket& packet)>;
  using PublishBackend = std::function<bool(const PublishEnvelope& envelope)>;
  using ShutdownObserver = std::function<void(std::size_t abandoned_requests)>;

  AccessGateway(SubmitPipeline submit_pipeline = {},
                PublishBackend publish_backend = {},
                ShutdownObserver shutdown_observer = {});

  [[nodiscard]] bool init() override;

  [[nodiscard]] RuntimeDispatchResult submit(const InboundPacket& packet) override;

  [[nodiscard]] bool publish_result(const PublishEnvelope& envelope) override;

  [[nodiscard]] AccessGatewayState state() const override;

  [[nodiscard]] bool is_ready() const override;

  void shutdown(std::chrono::milliseconds drain_timeout) override;

 private:
  class InflightGuard {
   public:
    explicit InflightGuard(AccessGateway* gateway);
    ~InflightGuard();

   private:
    AccessGateway* gateway_;
  };

  [[nodiscard]] RuntimeDispatchResult run_submit_pipeline(const InboundPacket& packet);

  [[nodiscard]] static RuntimeDispatchResult make_shutting_down_result();

  SubmitPipeline submit_pipeline_;
  PublishBackend publish_backend_;
  ShutdownObserver shutdown_observer_;

  std::atomic<AccessGatewayState> state_{AccessGatewayState::Uninitialized};

  mutable std::mutex inflight_mutex_;
  std::condition_variable inflight_drained_cv_;
  std::size_t inflight_requests_ = 0;
};

}  // namespace dasall::access
