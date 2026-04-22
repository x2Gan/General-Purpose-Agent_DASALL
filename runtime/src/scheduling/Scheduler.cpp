#include "Scheduler.h"

#include <utility>

namespace dasall::runtime {

Scheduler::Scheduler(
    const std::uint32_t recovery_queue_limit,
    const std::uint32_t maintenance_queue_limit)
    : recovery_queue_limit_(recovery_queue_limit),
      maintenance_queue_limit_(maintenance_queue_limit) {}

SchedulerEnqueueResult Scheduler::enqueue(const SchedulerTicketRequest& request) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (!request.has_minimum_requirements()) {
    return SchedulerEnqueueResult{
        .accepted = false,
        .ticket = std::nullopt,
        .applied_overflow_disposition = std::nullopt,
        .backpressure_state = compute_backpressure_state_locked(),
        .detail = "ticket_id and request_id are required",
    };
  }

  const auto next_ticket = make_scheduler_ticket(request, next_sequence_++);
  switch (request.priority_class) {
    case SchedulerPriorityClass::ForegroundInteractive:
      if (!foreground_queue_.empty()) {
        auto state = compute_backpressure_state_locked(
            SchedulerBackpressureSignal::ForegroundBusy);
        state.rejecting_foreground = true;
        return SchedulerEnqueueResult{
            .accepted = false,
            .ticket = std::nullopt,
            .applied_overflow_disposition = SchedulerOverflowDisposition::RejectNew,
            .backpressure_state = state,
            .detail = "foreground queue already has an active request",
        };
      }
      foreground_queue_.push_back(next_ticket);
      break;
    case SchedulerPriorityClass::Recovery:
      if (recovery_queue_.size() >= recovery_queue_limit_) {
        auto state = compute_backpressure_state_locked(
            SchedulerBackpressureSignal::RecoveryFailedSafe);
        state.failed_safe_recommended = true;
        return SchedulerEnqueueResult{
            .accepted = false,
            .ticket = std::nullopt,
            .applied_overflow_disposition = SchedulerOverflowDisposition::EnterFailedSafe,
            .backpressure_state = state,
            .detail = "recovery queue saturated; escalate to FailedSafe",
        };
      }
      recovery_queue_.push_back(next_ticket);
      break;
    case SchedulerPriorityClass::Maintenance:
      if (maintenance_queue_.size() >= maintenance_queue_limit_) {
        maintenance_queue_.pop_front();
        maintenance_queue_.push_back(next_ticket);
        auto state = compute_backpressure_state_locked(
            SchedulerBackpressureSignal::MaintenanceDropOldest);
        state.dropping_oldest_maintenance = true;
        return make_scheduler_enqueue_result(
            next_ticket,
            state,
            "maintenance queue dropped oldest ticket before accepting new work",
            SchedulerOverflowDisposition::DropOldest);
      }
      maintenance_queue_.push_back(next_ticket);
      break;
  }

  return make_scheduler_enqueue_result(
      next_ticket,
      compute_backpressure_state_locked(),
      "ticket accepted into scheduler queue");
}

AcquireWorkerResult Scheduler::acquire_worker(const AcquireWorkerRequest& request) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  worker_budget_ = request.worker_budget;
  if (!worker_budget_.has_capacity()) {
    return AcquireWorkerResult{
        .acquired = false,
        .ticket = std::nullopt,
        .backpressure_state = compute_backpressure_state_locked(
            SchedulerBackpressureSignal::WorkerPoolSaturated),
        .detail = "worker pool saturated",
    };
  }

  auto ticket = pop_next_ticket_locked(
      request.preferred_priority_class,
      request.preferred_ticket_id);
  if (!ticket.has_value()) {
    return AcquireWorkerResult{
        .acquired = false,
        .ticket = std::nullopt,
        .backpressure_state = compute_backpressure_state_locked(),
        .detail = "no ticket available for acquisition",
    };
  }

  ticket->assigned_worker_id = std::string("worker-") +
                               std::to_string(static_cast<unsigned>(worker_budget_.busy_workers + 1));
  ticket->state = SchedulerTicketState::WorkerAssigned;
  ++worker_budget_.busy_workers;
  return make_acquire_worker_result(
      *ticket,
      compute_backpressure_state_locked(),
      "worker acquired from scheduler queue");
}

ReleaseWorkerResult Scheduler::release_worker(const ReleaseWorkerRequest& request) {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (!request.ticket.has_worker_assignment()) {
    return ReleaseWorkerResult{
        .released = false,
        .backpressure_state = compute_backpressure_state_locked(),
        .detail = "release_worker requires an assigned worker ticket",
    };
  }

  if (worker_budget_.busy_workers > 0) {
    --worker_budget_.busy_workers;
  }

  return make_release_worker_result(
      compute_backpressure_state_locked(),
      request.worker_completed ? "worker released after completion"
                               : "worker released without completion");
}

SchedulerBackpressureState Scheduler::backpressure_state() const {
  std::lock_guard<std::mutex> lock(queue_mutex_);
  return compute_backpressure_state_locked();
}

SchedulerBackpressureState Scheduler::compute_backpressure_state_locked(
    const SchedulerBackpressureSignal dominant_signal) const {
  return SchedulerBackpressureState{
      .foreground_queue_depth = static_cast<std::uint32_t>(foreground_queue_.size()),
      .foreground_queue_limit = 1,
      .recovery_queue_depth = static_cast<std::uint32_t>(recovery_queue_.size()),
      .recovery_queue_limit = recovery_queue_limit_,
      .maintenance_queue_depth = static_cast<std::uint32_t>(maintenance_queue_.size()),
      .maintenance_queue_limit = maintenance_queue_limit_,
      .worker_budget = worker_budget_,
      .dominant_signal = dominant_signal,
      .rejecting_foreground = dominant_signal == SchedulerBackpressureSignal::ForegroundBusy,
      .failed_safe_recommended =
          dominant_signal == SchedulerBackpressureSignal::RecoveryFailedSafe,
      .dropping_oldest_maintenance =
          dominant_signal == SchedulerBackpressureSignal::MaintenanceDropOldest,
  };
}

std::optional<SchedulerTicket> Scheduler::pop_next_ticket_locked(
    const std::optional<SchedulerPriorityClass>& preferred_priority_class,
    const std::optional<std::string>& preferred_ticket_id) {
  if (preferred_ticket_id.has_value()) {
    if (const auto ticket = pop_ticket_by_id_locked(foreground_queue_, *preferred_ticket_id)) {
      return ticket;
    }
    if (const auto ticket = pop_ticket_by_id_locked(recovery_queue_, *preferred_ticket_id)) {
      return ticket;
    }
    if (const auto ticket = pop_ticket_by_id_locked(maintenance_queue_, *preferred_ticket_id)) {
      return ticket;
    }
  }

  if (preferred_priority_class.has_value()) {
    switch (*preferred_priority_class) {
      case SchedulerPriorityClass::ForegroundInteractive:
        return pop_front_ticket_locked(foreground_queue_);
      case SchedulerPriorityClass::Recovery:
        return pop_front_ticket_locked(recovery_queue_);
      case SchedulerPriorityClass::Maintenance:
        return pop_front_ticket_locked(maintenance_queue_);
    }
  }

  if (const auto ticket = pop_front_ticket_locked(foreground_queue_)) {
    return ticket;
  }
  if (const auto ticket = pop_front_ticket_locked(recovery_queue_)) {
    return ticket;
  }
  return pop_front_ticket_locked(maintenance_queue_);
}

std::optional<SchedulerTicket> Scheduler::pop_front_ticket_locked(
    std::deque<SchedulerTicket>& queue) {
  if (queue.empty()) {
    return std::nullopt;
  }

  auto ticket = std::move(queue.front());
  queue.pop_front();
  return ticket;
}

std::optional<SchedulerTicket> Scheduler::pop_ticket_by_id_locked(
    std::deque<SchedulerTicket>& queue,
    const std::string& ticket_id) {
  for (auto it = queue.begin(); it != queue.end(); ++it) {
    if (it->ticket_id == ticket_id) {
      auto ticket = std::move(*it);
      queue.erase(it);
      return ticket;
    }
  }

  return std::nullopt;
}

}  // namespace dasall::runtime