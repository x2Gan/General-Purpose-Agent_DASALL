#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "CancellationToken.h"

namespace dasall::runtime {

enum class SchedulerPriorityClass : std::uint8_t {
  ForegroundInteractive = 0,
  Recovery,
  Maintenance,
};

[[nodiscard]] constexpr const char* scheduler_priority_class_name(
    const SchedulerPriorityClass priority_class) {
  switch (priority_class) {
    case SchedulerPriorityClass::ForegroundInteractive:
      return "ForegroundInteractive";
    case SchedulerPriorityClass::Recovery:
      return "Recovery";
    case SchedulerPriorityClass::Maintenance:
      return "Maintenance";
  }

  return "Unknown";
}

enum class SchedulerOverflowDisposition : std::uint8_t {
  RejectNew = 0,
  EnterFailedSafe,
  DropOldest,
};

[[nodiscard]] constexpr const char* scheduler_overflow_disposition_name(
    const SchedulerOverflowDisposition disposition) {
  switch (disposition) {
    case SchedulerOverflowDisposition::RejectNew:
      return "RejectNew";
    case SchedulerOverflowDisposition::EnterFailedSafe:
      return "EnterFailedSafe";
    case SchedulerOverflowDisposition::DropOldest:
      return "DropOldest";
  }

  return "Unknown";
}

[[nodiscard]] constexpr SchedulerOverflowDisposition scheduler_overflow_disposition(
    const SchedulerPriorityClass priority_class) {
  switch (priority_class) {
    case SchedulerPriorityClass::ForegroundInteractive:
      return SchedulerOverflowDisposition::RejectNew;
    case SchedulerPriorityClass::Recovery:
      return SchedulerOverflowDisposition::EnterFailedSafe;
    case SchedulerPriorityClass::Maintenance:
      return SchedulerOverflowDisposition::DropOldest;
  }

  return SchedulerOverflowDisposition::RejectNew;
}

enum class SchedulerTicketState : std::uint8_t {
  Queued = 0,
  WorkerAssigned,
  Released,
  Rejected,
};

[[nodiscard]] constexpr const char* scheduler_ticket_state_name(
    const SchedulerTicketState state) {
  switch (state) {
    case SchedulerTicketState::Queued:
      return "Queued";
    case SchedulerTicketState::WorkerAssigned:
      return "WorkerAssigned";
    case SchedulerTicketState::Released:
      return "Released";
    case SchedulerTicketState::Rejected:
      return "Rejected";
  }

  return "Unknown";
}

enum class SchedulerBackpressureSignal : std::uint8_t {
  None = 0,
  ForegroundBusy,
  RecoveryFailedSafe,
  MaintenanceDropOldest,
  WorkerPoolSaturated,
};

[[nodiscard]] constexpr const char* scheduler_backpressure_signal_name(
    const SchedulerBackpressureSignal signal) {
  switch (signal) {
    case SchedulerBackpressureSignal::None:
      return "None";
    case SchedulerBackpressureSignal::ForegroundBusy:
      return "ForegroundBusy";
    case SchedulerBackpressureSignal::RecoveryFailedSafe:
      return "RecoveryFailedSafe";
    case SchedulerBackpressureSignal::MaintenanceDropOldest:
      return "MaintenanceDropOldest";
    case SchedulerBackpressureSignal::WorkerPoolSaturated:
      return "WorkerPoolSaturated";
  }

  return "Unknown";
}

struct WorkerLeaseBudget {
  std::uint32_t max_workers = 1;
  std::uint32_t busy_workers = 0;

  [[nodiscard]] bool has_capacity() const {
    return busy_workers < max_workers;
  }
};

struct SchedulerTicketRequest {
  std::string ticket_id;
  std::string request_id;
  std::optional<std::string> session_id;
  SchedulerPriorityClass priority_class = SchedulerPriorityClass::ForegroundInteractive;
  CancellationToken cancellation_token;
  std::optional<std::string> checkpoint_ref;
  std::optional<std::string> queue_key;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !ticket_id.empty() && !request_id.empty();
  }
};

struct SchedulerTicket {
  std::string ticket_id;
  std::string request_id;
  std::optional<std::string> session_id;
  SchedulerPriorityClass priority_class = SchedulerPriorityClass::ForegroundInteractive;
  CancellationToken cancellation_token;
  std::optional<std::string> checkpoint_ref;
  std::optional<std::string> queue_key;
  std::optional<std::string> assigned_worker_id;
  std::uint64_t enqueue_sequence = 0;
  SchedulerTicketState state = SchedulerTicketState::Queued;

  [[nodiscard]] bool has_worker_assignment() const {
    return assigned_worker_id.has_value() && !assigned_worker_id->empty();
  }
};

struct SchedulerBackpressureState {
  std::uint32_t foreground_queue_depth = 0;
  std::uint32_t foreground_queue_limit = 1;
  std::uint32_t recovery_queue_depth = 0;
  std::uint32_t recovery_queue_limit = 0;
  std::uint32_t maintenance_queue_depth = 0;
  std::uint32_t maintenance_queue_limit = 16;
  WorkerLeaseBudget worker_budget;
  SchedulerBackpressureSignal dominant_signal = SchedulerBackpressureSignal::None;
  bool rejecting_foreground = false;
  bool failed_safe_recommended = false;
  bool dropping_oldest_maintenance = false;

  [[nodiscard]] bool workers_saturated() const {
    return !worker_budget.has_capacity();
  }

  [[nodiscard]] bool overloaded() const {
    return dominant_signal != SchedulerBackpressureSignal::None || rejecting_foreground ||
           failed_safe_recommended || dropping_oldest_maintenance || workers_saturated();
  }
};

[[nodiscard]] inline SchedulerTicket make_scheduler_ticket(
    const SchedulerTicketRequest& request,
    const std::uint64_t enqueue_sequence,
    const SchedulerTicketState state = SchedulerTicketState::Queued,
    const std::optional<std::string>& assigned_worker_id = std::nullopt) {
  return SchedulerTicket{
      .ticket_id = request.ticket_id,
      .request_id = request.request_id,
      .session_id = request.session_id,
      .priority_class = request.priority_class,
      .cancellation_token = request.cancellation_token,
      .checkpoint_ref = request.checkpoint_ref,
      .queue_key = request.queue_key,
      .assigned_worker_id = assigned_worker_id,
      .enqueue_sequence = enqueue_sequence,
      .state = state,
  };
}

}  // namespace dasall::runtime