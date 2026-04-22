#pragma once

#include <optional>
#include <string>

#include "scheduling/SchedulerTicket.h"

namespace dasall::runtime {

struct AcquireWorkerRequest {
  WorkerLeaseBudget worker_budget;
  std::optional<SchedulerPriorityClass> preferred_priority_class;
  std::optional<std::string> preferred_ticket_id;
};

struct SchedulerEnqueueResult {
  bool accepted = false;
  std::optional<SchedulerTicket> ticket;
  std::optional<SchedulerOverflowDisposition> applied_overflow_disposition;
  SchedulerBackpressureState backpressure_state;
  std::string detail;

  [[nodiscard]] bool has_ticket() const {
    return accepted && ticket.has_value();
  }
};

struct AcquireWorkerResult {
  bool acquired = false;
  std::optional<SchedulerTicket> ticket;
  SchedulerBackpressureState backpressure_state;
  std::string detail;

  [[nodiscard]] bool has_ticket() const {
    return acquired && ticket.has_value();
  }
};

struct ReleaseWorkerRequest {
  SchedulerTicket ticket;
  bool worker_completed = true;
};

struct ReleaseWorkerResult {
  bool released = false;
  SchedulerBackpressureState backpressure_state;
  std::string detail;
};

[[nodiscard]] inline SchedulerEnqueueResult make_scheduler_enqueue_result(
    const SchedulerTicket& ticket,
    const SchedulerBackpressureState& backpressure_state,
    const std::string& detail = std::string(),
    const std::optional<SchedulerOverflowDisposition>& applied_overflow_disposition =
        std::nullopt) {
  return SchedulerEnqueueResult{
      .accepted = true,
      .ticket = ticket,
      .applied_overflow_disposition = applied_overflow_disposition,
      .backpressure_state = backpressure_state,
      .detail = detail,
  };
}

[[nodiscard]] inline AcquireWorkerResult make_acquire_worker_result(
    const SchedulerTicket& ticket,
    const SchedulerBackpressureState& backpressure_state,
    const std::string& detail = std::string()) {
  return AcquireWorkerResult{
      .acquired = true,
      .ticket = ticket,
      .backpressure_state = backpressure_state,
      .detail = detail,
  };
}

[[nodiscard]] inline ReleaseWorkerResult make_release_worker_result(
    const SchedulerBackpressureState& backpressure_state,
    const std::string& detail = std::string()) {
  return ReleaseWorkerResult{
      .released = true,
      .backpressure_state = backpressure_state,
      .detail = detail,
  };
}

class IScheduler {
 public:
  virtual ~IScheduler() = default;

  [[nodiscard]] virtual SchedulerEnqueueResult enqueue(
      const SchedulerTicketRequest& request) = 0;

  [[nodiscard]] virtual AcquireWorkerResult acquire_worker(
      const AcquireWorkerRequest& request) = 0;

  [[nodiscard]] virtual ReleaseWorkerResult release_worker(
      const ReleaseWorkerRequest& request) = 0;

  [[nodiscard]] virtual SchedulerBackpressureState backpressure_state() const = 0;
};

}  // namespace dasall::runtime