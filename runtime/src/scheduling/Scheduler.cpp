#include "Scheduler.h"

#include <utility>

#include "../logging/RuntimeStructuredLogUtils.h"

namespace dasall::runtime {

Scheduler::Scheduler(
    const std::uint32_t recovery_queue_limit,
    const std::uint32_t maintenance_queue_limit)
    : recovery_queue_limit_(recovery_queue_limit),
      maintenance_queue_limit_(maintenance_queue_limit) {}

void Scheduler::set_logger(
    std::shared_ptr<infra::logging::ILogger> logger,
    std::optional<std::string> runtime_instance_id) {
  logger_ = std::move(logger);
  runtime_instance_id_ = std::move(runtime_instance_id);
}

SchedulerEnqueueResult Scheduler::enqueue(const SchedulerTicketRequest& request) {
  SchedulerEnqueueResult result;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!request.has_minimum_requirements()) {
      result = SchedulerEnqueueResult{
          .accepted = false,
          .ticket = std::nullopt,
          .applied_overflow_disposition = std::nullopt,
          .backpressure_state = compute_backpressure_state_locked(),
          .detail = "ticket_id and request_id are required",
      };
    } else {
      const auto next_ticket = make_scheduler_ticket(request, next_sequence_++);
      switch (request.priority_class) {
        case SchedulerPriorityClass::ForegroundInteractive:
          if (!foreground_queue_.empty()) {
            auto state = compute_backpressure_state_locked(
                SchedulerBackpressureSignal::ForegroundBusy);
            state.rejecting_foreground = true;
            result = SchedulerEnqueueResult{
                .accepted = false,
                .ticket = std::nullopt,
                .applied_overflow_disposition = SchedulerOverflowDisposition::RejectNew,
                .backpressure_state = state,
                .detail = "foreground queue already has an active request",
            };
            break;
          }
          foreground_queue_.push_back(next_ticket);
          result = make_scheduler_enqueue_result(
              next_ticket,
              compute_backpressure_state_locked(),
              "ticket accepted into scheduler queue");
          break;
        case SchedulerPriorityClass::Recovery:
          if (recovery_queue_.size() >= recovery_queue_limit_) {
            auto state = compute_backpressure_state_locked(
                SchedulerBackpressureSignal::RecoveryFailedSafe);
            state.failed_safe_recommended = true;
            result = SchedulerEnqueueResult{
                .accepted = false,
                .ticket = std::nullopt,
                .applied_overflow_disposition = SchedulerOverflowDisposition::EnterFailedSafe,
                .backpressure_state = state,
                .detail = "recovery queue saturated; escalate to FailedSafe",
            };
            break;
          }
          recovery_queue_.push_back(next_ticket);
          result = make_scheduler_enqueue_result(
              next_ticket,
              compute_backpressure_state_locked(),
              "ticket accepted into scheduler queue");
          break;
        case SchedulerPriorityClass::Maintenance:
          if (maintenance_queue_.size() >= maintenance_queue_limit_) {
            maintenance_queue_.pop_front();
            maintenance_queue_.push_back(next_ticket);
            auto state = compute_backpressure_state_locked(
                SchedulerBackpressureSignal::MaintenanceDropOldest);
            state.dropping_oldest_maintenance = true;
            result = make_scheduler_enqueue_result(
                next_ticket,
                state,
                "maintenance queue dropped oldest ticket before accepting new work",
                SchedulerOverflowDisposition::DropOldest);
            break;
          }
          maintenance_queue_.push_back(next_ticket);
          result = make_scheduler_enqueue_result(
              next_ticket,
              compute_backpressure_state_locked(),
              "ticket accepted into scheduler queue");
          break;
      }
    }
  }

  infra::LogEvent::AttributeMap attrs;
  detail::add_string_attr(attrs, "operation", "enqueue");
  detail::add_string_attr(attrs, "ticket_id", request.ticket_id);
  detail::add_string_attr(attrs, "request_id", request.request_id);
  detail::add_optional_string_attr(attrs, "session_id", request.session_id);
  detail::add_optional_string_attr(attrs, "checkpoint_ref", request.checkpoint_ref);
  detail::add_optional_string_attr(attrs, "queue_key", request.queue_key);
  detail::add_string_attr(
      attrs,
      "priority_class",
      scheduler_priority_class_name(request.priority_class));
  detail::add_bool_attr(attrs, "accepted", result.accepted);
  if (result.ticket.has_value()) {
    detail::add_string_attr(
        attrs,
        "ticket_state",
        scheduler_ticket_state_name(result.ticket->state));
  }
  if (result.applied_overflow_disposition.has_value()) {
    detail::add_string_attr(
        attrs,
        "applied_overflow_disposition",
        scheduler_overflow_disposition_name(*result.applied_overflow_disposition));
  }
  detail::add_string_attr(
      attrs,
      "dominant_signal",
      scheduler_backpressure_signal_name(result.backpressure_state.dominant_signal));
  detail::add_integer_attr(
      attrs,
      "foreground_queue_depth",
      result.backpressure_state.foreground_queue_depth);
  detail::add_integer_attr(
      attrs,
      "recovery_queue_depth",
      result.backpressure_state.recovery_queue_depth);
  detail::add_integer_attr(
      attrs,
      "maintenance_queue_depth",
      result.backpressure_state.maintenance_queue_depth);
  detail::add_integer_attr(
      attrs,
      "busy_workers",
      result.backpressure_state.worker_budget.busy_workers);
  detail::add_integer_attr(
      attrs,
      "max_workers",
      result.backpressure_state.worker_budget.max_workers);
  detail::add_string_attr(attrs, "detail", result.detail);
  detail::emit_runtime_log(
      logger_,
      (!result.accepted || result.applied_overflow_disposition.has_value())
          ? infra::LogLevel::Warn
          : infra::LogLevel::Info,
      "runtime.scheduler.enqueue",
      "scheduler",
      runtime_instance_id_,
      std::move(attrs));
  return result;
}

AcquireWorkerResult Scheduler::acquire_worker(const AcquireWorkerRequest& request) {
  AcquireWorkerResult result;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    worker_budget_ = request.worker_budget;
    if (!worker_budget_.has_capacity()) {
      result = AcquireWorkerResult{
          .acquired = false,
          .ticket = std::nullopt,
          .backpressure_state = compute_backpressure_state_locked(
              SchedulerBackpressureSignal::WorkerPoolSaturated),
          .detail = "worker pool saturated",
      };
    } else {
      auto ticket = pop_next_ticket_locked(
          request.preferred_priority_class,
          request.preferred_ticket_id);
      if (!ticket.has_value()) {
        result = AcquireWorkerResult{
            .acquired = false,
            .ticket = std::nullopt,
            .backpressure_state = compute_backpressure_state_locked(),
            .detail = "no ticket available for acquisition",
        };
      } else {
        ticket->assigned_worker_id = std::string("worker-") +
                                     std::to_string(
                                         static_cast<unsigned>(worker_budget_.busy_workers + 1));
        ticket->state = SchedulerTicketState::WorkerAssigned;
        ++worker_budget_.busy_workers;
        result = make_acquire_worker_result(
            *ticket,
            compute_backpressure_state_locked(),
            "worker acquired from scheduler queue");
      }
    }
  }

  infra::LogEvent::AttributeMap attrs;
  detail::add_string_attr(attrs, "operation", "acquire_worker");
  detail::add_integer_attr(attrs, "requested_max_workers", request.worker_budget.max_workers);
  detail::add_integer_attr(attrs, "requested_busy_workers", request.worker_budget.busy_workers);
  if (request.preferred_priority_class.has_value()) {
    detail::add_string_attr(
        attrs,
        "preferred_priority_class",
        scheduler_priority_class_name(*request.preferred_priority_class));
  }
  detail::add_optional_string_attr(attrs, "preferred_ticket_id", request.preferred_ticket_id);
  detail::add_bool_attr(attrs, "acquired", result.acquired);
  if (result.ticket.has_value()) {
    detail::add_string_attr(attrs, "ticket_id", result.ticket->ticket_id);
    detail::add_string_attr(
        attrs,
        "ticket_priority_class",
        scheduler_priority_class_name(result.ticket->priority_class));
    detail::add_optional_string_attr(attrs, "assigned_worker_id", result.ticket->assigned_worker_id);
  }
  detail::add_string_attr(
      attrs,
      "dominant_signal",
      scheduler_backpressure_signal_name(result.backpressure_state.dominant_signal));
  detail::add_integer_attr(
      attrs,
      "foreground_queue_depth",
      result.backpressure_state.foreground_queue_depth);
  detail::add_integer_attr(
      attrs,
      "recovery_queue_depth",
      result.backpressure_state.recovery_queue_depth);
  detail::add_integer_attr(
      attrs,
      "maintenance_queue_depth",
      result.backpressure_state.maintenance_queue_depth);
  detail::add_integer_attr(
      attrs,
      "busy_workers",
      result.backpressure_state.worker_budget.busy_workers);
  detail::add_integer_attr(
      attrs,
      "max_workers",
      result.backpressure_state.worker_budget.max_workers);
  detail::add_string_attr(attrs, "detail", result.detail);
  detail::emit_runtime_log(
      logger_,
      (!result.acquired && result.backpressure_state.overloaded())
          ? infra::LogLevel::Warn
          : infra::LogLevel::Info,
      "runtime.scheduler.acquire_worker",
      "scheduler",
      runtime_instance_id_,
      std::move(attrs));
  return result;
}

ReleaseWorkerResult Scheduler::release_worker(const ReleaseWorkerRequest& request) {
  ReleaseWorkerResult result;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (!request.ticket.has_worker_assignment()) {
      result = ReleaseWorkerResult{
          .released = false,
          .backpressure_state = compute_backpressure_state_locked(),
          .detail = "release_worker requires an assigned worker ticket",
      };
    } else {
      if (worker_budget_.busy_workers > 0) {
        --worker_budget_.busy_workers;
      }

      result = make_release_worker_result(
          compute_backpressure_state_locked(),
          request.worker_completed ? "worker released after completion"
                                   : "worker released without completion");
    }
  }

  infra::LogEvent::AttributeMap attrs;
  detail::add_string_attr(attrs, "operation", "release_worker");
  detail::add_string_attr(attrs, "ticket_id", request.ticket.ticket_id);
  detail::add_string_attr(attrs, "request_id", request.ticket.request_id);
  detail::add_string_attr(
      attrs,
      "priority_class",
      scheduler_priority_class_name(request.ticket.priority_class));
  detail::add_optional_string_attr(attrs, "assigned_worker_id", request.ticket.assigned_worker_id);
  detail::add_bool_attr(attrs, "worker_completed", request.worker_completed);
  detail::add_bool_attr(attrs, "released", result.released);
  detail::add_string_attr(
      attrs,
      "dominant_signal",
      scheduler_backpressure_signal_name(result.backpressure_state.dominant_signal));
  detail::add_integer_attr(
      attrs,
      "busy_workers",
      result.backpressure_state.worker_budget.busy_workers);
  detail::add_integer_attr(
      attrs,
      "max_workers",
      result.backpressure_state.worker_budget.max_workers);
  detail::add_string_attr(attrs, "detail", result.detail);
  detail::emit_runtime_log(
      logger_,
      result.released ? infra::LogLevel::Info : infra::LogLevel::Warn,
      "runtime.scheduler.release_worker",
      "scheduler",
      runtime_instance_id_,
      std::move(attrs));
  return result;
}

SchedulerBackpressureState Scheduler::backpressure_state() const {
  SchedulerBackpressureState state;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    state = compute_backpressure_state_locked();
  }

  infra::LogEvent::AttributeMap attrs;
  detail::add_string_attr(attrs, "operation", "backpressure_state");
  detail::add_string_attr(
      attrs,
      "dominant_signal",
      scheduler_backpressure_signal_name(state.dominant_signal));
  detail::add_integer_attr(attrs, "foreground_queue_depth", state.foreground_queue_depth);
  detail::add_integer_attr(attrs, "foreground_queue_limit", state.foreground_queue_limit);
  detail::add_integer_attr(attrs, "recovery_queue_depth", state.recovery_queue_depth);
  detail::add_integer_attr(attrs, "recovery_queue_limit", state.recovery_queue_limit);
  detail::add_integer_attr(attrs, "maintenance_queue_depth", state.maintenance_queue_depth);
  detail::add_integer_attr(attrs, "maintenance_queue_limit", state.maintenance_queue_limit);
  detail::add_integer_attr(attrs, "busy_workers", state.worker_budget.busy_workers);
  detail::add_integer_attr(attrs, "max_workers", state.worker_budget.max_workers);
  detail::add_bool_attr(attrs, "overloaded", state.overloaded());
  detail::emit_runtime_log(
      logger_,
      state.overloaded() ? infra::LogLevel::Warn : infra::LogLevel::Info,
      "runtime.scheduler.backpressure_state",
      "scheduler",
      runtime_instance_id_,
      std::move(attrs));
  return state;
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