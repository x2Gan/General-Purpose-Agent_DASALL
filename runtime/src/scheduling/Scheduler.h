#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "scheduling/IScheduler.h"

namespace dasall::infra::logging {
class ILogger;
}

namespace dasall::runtime {

class Scheduler final : public IScheduler {
 public:
  explicit Scheduler(
      std::uint32_t recovery_queue_limit = 2,
      std::uint32_t maintenance_queue_limit = 16);

  void set_logger(
      std::shared_ptr<infra::logging::ILogger> logger,
      std::optional<std::string> runtime_instance_id = std::nullopt);

  [[nodiscard]] SchedulerEnqueueResult enqueue(
      const SchedulerTicketRequest& request) override;

  [[nodiscard]] AcquireWorkerResult acquire_worker(
      const AcquireWorkerRequest& request) override;

  [[nodiscard]] ReleaseWorkerResult release_worker(
      const ReleaseWorkerRequest& request) override;

  [[nodiscard]] SchedulerBackpressureState backpressure_state() const override;

 private:
  [[nodiscard]] SchedulerBackpressureState compute_backpressure_state_locked(
      SchedulerBackpressureSignal dominant_signal = SchedulerBackpressureSignal::None) const;

  [[nodiscard]] std::optional<SchedulerTicket> pop_next_ticket_locked(
      const std::optional<SchedulerPriorityClass>& preferred_priority_class,
      const std::optional<std::string>& preferred_ticket_id);

  [[nodiscard]] static std::optional<SchedulerTicket> pop_front_ticket_locked(
      std::deque<SchedulerTicket>& queue);

  [[nodiscard]] static std::optional<SchedulerTicket> pop_ticket_by_id_locked(
      std::deque<SchedulerTicket>& queue,
      const std::string& ticket_id);

  mutable std::mutex queue_mutex_;
  std::deque<SchedulerTicket> foreground_queue_;
  std::deque<SchedulerTicket> recovery_queue_;
  std::deque<SchedulerTicket> maintenance_queue_;
  WorkerLeaseBudget worker_budget_;
  std::uint64_t next_sequence_ = 1;
  std::uint32_t recovery_queue_limit_ = 2;
  std::uint32_t maintenance_queue_limit_ = 16;
    std::shared_ptr<infra::logging::ILogger> logger_;
    std::optional<std::string> runtime_instance_id_;
};

}  // namespace dasall::runtime