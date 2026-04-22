#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "scheduling/IScheduler.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::runtime::SchedulerTicketRequest make_ticket_request(
  const std::string& ticket_id,
  const std::string& request_id,
  const dasall::runtime::SchedulerPriorityClass priority_class,
  const std::optional<std::string>& session_id = std::nullopt,
  const std::optional<std::string>& checkpoint_ref = std::nullopt,
  const std::optional<std::string>& queue_key = std::nullopt,
  const dasall::runtime::CancellationToken& cancellation_token =
    dasall::runtime::CancellationToken()) {
  return dasall::runtime::SchedulerTicketRequest{
    .ticket_id = ticket_id,
    .request_id = request_id,
    .session_id = session_id,
    .priority_class = priority_class,
    .cancellation_token = cancellation_token,
    .checkpoint_ref = checkpoint_ref,
    .queue_key = queue_key,
  };
}

class FakeScheduler final : public dasall::runtime::IScheduler {
 public:
  FakeScheduler(
      const std::uint32_t recovery_queue_limit = 2,
      const std::uint32_t maintenance_queue_limit = 2)
      : recovery_queue_limit_(recovery_queue_limit),
        maintenance_queue_limit_(maintenance_queue_limit) {}

  [[nodiscard]] dasall::runtime::SchedulerEnqueueResult enqueue(
      const dasall::runtime::SchedulerTicketRequest& request) override {
    if (!request.has_minimum_requirements()) {
      return dasall::runtime::SchedulerEnqueueResult{
          .accepted = false,
          .ticket = std::nullopt,
          .applied_overflow_disposition = std::nullopt,
          .backpressure_state = compute_backpressure_state(),
          .detail = "ticket_id and request_id are required",
      };
    }

    const auto next_ticket = dasall::runtime::make_scheduler_ticket(request, next_sequence_++);
    switch (request.priority_class) {
      case dasall::runtime::SchedulerPriorityClass::ForegroundInteractive:
        if (!foreground_queue_.empty()) {
          auto state = compute_backpressure_state(
              dasall::runtime::SchedulerBackpressureSignal::ForegroundBusy);
          state.rejecting_foreground = true;
          return dasall::runtime::SchedulerEnqueueResult{
              .accepted = false,
              .ticket = std::nullopt,
              .applied_overflow_disposition =
                  dasall::runtime::SchedulerOverflowDisposition::RejectNew,
              .backpressure_state = state,
              .detail = "foreground queue already has an active request",
          };
        }
        foreground_queue_.push_back(next_ticket);
        break;
      case dasall::runtime::SchedulerPriorityClass::Recovery:
        if (recovery_queue_.size() >= recovery_queue_limit_) {
          auto state = compute_backpressure_state(
              dasall::runtime::SchedulerBackpressureSignal::RecoveryFailedSafe);
          state.failed_safe_recommended = true;
          return dasall::runtime::SchedulerEnqueueResult{
              .accepted = false,
              .ticket = std::nullopt,
              .applied_overflow_disposition =
                  dasall::runtime::SchedulerOverflowDisposition::EnterFailedSafe,
              .backpressure_state = state,
              .detail = "recovery queue saturated; escalate to FailedSafe",
          };
        }
        recovery_queue_.push_back(next_ticket);
        break;
      case dasall::runtime::SchedulerPriorityClass::Maintenance:
        if (maintenance_queue_.size() >= maintenance_queue_limit_) {
          maintenance_queue_.pop_front();
          maintenance_queue_.push_back(next_ticket);
          auto state = compute_backpressure_state(
              dasall::runtime::SchedulerBackpressureSignal::MaintenanceDropOldest);
          state.dropping_oldest_maintenance = true;
          return dasall::runtime::make_scheduler_enqueue_result(
              next_ticket,
              state,
              "maintenance queue dropped oldest ticket before accepting new work",
              dasall::runtime::SchedulerOverflowDisposition::DropOldest);
        }
        maintenance_queue_.push_back(next_ticket);
        break;
    }

    return dasall::runtime::make_scheduler_enqueue_result(
        next_ticket,
        compute_backpressure_state(),
        "ticket accepted into fake scheduler queue");
  }

  [[nodiscard]] dasall::runtime::AcquireWorkerResult acquire_worker(
      const dasall::runtime::AcquireWorkerRequest& request) override {
    worker_budget_ = request.worker_budget;
    if (!worker_budget_.has_capacity()) {
      return dasall::runtime::AcquireWorkerResult{
          .acquired = false,
          .ticket = std::nullopt,
          .backpressure_state = compute_backpressure_state(
              dasall::runtime::SchedulerBackpressureSignal::WorkerPoolSaturated),
          .detail = "worker pool saturated",
      };
    }

    auto ticket = pop_next_ticket(request.preferred_priority_class, request.preferred_ticket_id);
    if (!ticket.has_value()) {
      return dasall::runtime::AcquireWorkerResult{
          .acquired = false,
          .ticket = std::nullopt,
          .backpressure_state = compute_backpressure_state(),
          .detail = "no ticket available for acquisition",
      };
    }

    ticket->assigned_worker_id = std::string("worker-") +
                                 std::to_string(static_cast<unsigned>(worker_budget_.busy_workers + 1));
    ticket->state = dasall::runtime::SchedulerTicketState::WorkerAssigned;
    ++worker_budget_.busy_workers;
    return dasall::runtime::make_acquire_worker_result(
        *ticket,
        compute_backpressure_state(),
        "worker acquired from fake scheduler queue");
  }

  [[nodiscard]] dasall::runtime::ReleaseWorkerResult release_worker(
      const dasall::runtime::ReleaseWorkerRequest& request) override {
    if (!request.ticket.has_worker_assignment()) {
      return dasall::runtime::ReleaseWorkerResult{
          .released = false,
          .backpressure_state = compute_backpressure_state(),
          .detail = "release_worker requires an assigned worker ticket",
      };
    }

    if (worker_budget_.busy_workers > 0) {
      --worker_budget_.busy_workers;
    }

    return dasall::runtime::make_release_worker_result(
        compute_backpressure_state(),
        request.worker_completed ? "worker released after completion"
                                 : "worker released without completion");
  }

  [[nodiscard]] dasall::runtime::SchedulerBackpressureState backpressure_state() const override {
    return compute_backpressure_state();
  }

 private:
  [[nodiscard]] dasall::runtime::SchedulerBackpressureState compute_backpressure_state(
      const dasall::runtime::SchedulerBackpressureSignal dominant_signal =
          dasall::runtime::SchedulerBackpressureSignal::None) const {
    return dasall::runtime::SchedulerBackpressureState{
        .foreground_queue_depth = static_cast<std::uint32_t>(foreground_queue_.size()),
        .foreground_queue_limit = 1,
        .recovery_queue_depth = static_cast<std::uint32_t>(recovery_queue_.size()),
        .recovery_queue_limit = recovery_queue_limit_,
        .maintenance_queue_depth = static_cast<std::uint32_t>(maintenance_queue_.size()),
        .maintenance_queue_limit = maintenance_queue_limit_,
        .worker_budget = worker_budget_,
        .dominant_signal = dominant_signal,
        .rejecting_foreground =
            dominant_signal == dasall::runtime::SchedulerBackpressureSignal::ForegroundBusy,
        .failed_safe_recommended =
            dominant_signal == dasall::runtime::SchedulerBackpressureSignal::RecoveryFailedSafe,
        .dropping_oldest_maintenance =
            dominant_signal == dasall::runtime::SchedulerBackpressureSignal::MaintenanceDropOldest,
    };
  }

  [[nodiscard]] std::optional<dasall::runtime::SchedulerTicket> pop_next_ticket(
      const std::optional<dasall::runtime::SchedulerPriorityClass>& preferred_priority_class,
      const std::optional<std::string>& preferred_ticket_id) {
    if (preferred_ticket_id.has_value()) {
      if (const auto ticket = pop_ticket_by_id(foreground_queue_, *preferred_ticket_id)) {
        return ticket;
      }
      if (const auto ticket = pop_ticket_by_id(recovery_queue_, *preferred_ticket_id)) {
        return ticket;
      }
      if (const auto ticket = pop_ticket_by_id(maintenance_queue_, *preferred_ticket_id)) {
        return ticket;
      }
    }

    if (preferred_priority_class.has_value()) {
      switch (*preferred_priority_class) {
        case dasall::runtime::SchedulerPriorityClass::ForegroundInteractive:
          return pop_front_ticket(foreground_queue_);
        case dasall::runtime::SchedulerPriorityClass::Recovery:
          return pop_front_ticket(recovery_queue_);
        case dasall::runtime::SchedulerPriorityClass::Maintenance:
          return pop_front_ticket(maintenance_queue_);
      }
    }

    if (const auto ticket = pop_front_ticket(foreground_queue_)) {
      return ticket;
    }
    if (const auto ticket = pop_front_ticket(recovery_queue_)) {
      return ticket;
    }
    return pop_front_ticket(maintenance_queue_);
  }

  [[nodiscard]] static std::optional<dasall::runtime::SchedulerTicket> pop_front_ticket(
      std::deque<dasall::runtime::SchedulerTicket>& queue) {
    if (queue.empty()) {
      return std::nullopt;
    }

    auto ticket = queue.front();
    queue.pop_front();
    return ticket;
  }

  [[nodiscard]] static std::optional<dasall::runtime::SchedulerTicket> pop_ticket_by_id(
      std::deque<dasall::runtime::SchedulerTicket>& queue,
      const std::string& ticket_id) {
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (it->ticket_id == ticket_id) {
        auto ticket = *it;
        queue.erase(it);
        return ticket;
      }
    }

    return std::nullopt;
  }

  std::deque<dasall::runtime::SchedulerTicket> foreground_queue_;
  std::deque<dasall::runtime::SchedulerTicket> recovery_queue_;
  std::deque<dasall::runtime::SchedulerTicket> maintenance_queue_;
  dasall::runtime::WorkerLeaseBudget worker_budget_;
  std::uint64_t next_sequence_ = 1;
  std::uint32_t recovery_queue_limit_ = 2;
  std::uint32_t maintenance_queue_limit_ = 2;
};

}  // namespace

int main() {
  using dasall::runtime::AcquireWorkerRequest;
  using dasall::runtime::ReleaseWorkerRequest;
  using dasall::runtime::SchedulerBackpressureSignal;
  using dasall::runtime::SchedulerOverflowDisposition;
  using dasall::runtime::SchedulerPriorityClass;
  using dasall::runtime::SchedulerTicketRequest;
  using dasall::runtime::WorkerLeaseBudget;
  using dasall::runtime::scheduler_backpressure_signal_name;
  using dasall::runtime::scheduler_overflow_disposition;
  using dasall::runtime::scheduler_overflow_disposition_name;
  using dasall::runtime::scheduler_priority_class_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    assert_equal(
        "ForegroundInteractive",
        std::string(
            scheduler_priority_class_name(SchedulerPriorityClass::ForegroundInteractive)),
        "priority class enum should expose stable foreground name");
    assert_equal(
        "DropOldest",
        std::string(scheduler_overflow_disposition_name(
            scheduler_overflow_disposition(SchedulerPriorityClass::Maintenance))),
        "maintenance queue should keep stable drop-oldest overflow policy");
    assert_equal(
        "WorkerPoolSaturated",
        std::string(scheduler_backpressure_signal_name(
            SchedulerBackpressureSignal::WorkerPoolSaturated)),
        "backpressure signal enum should expose stable worker saturation name");

    FakeScheduler foreground_scheduler;
    const auto foreground_accepted = foreground_scheduler.enqueue(make_ticket_request(
      "fg-1",
      "req-fg-1",
      SchedulerPriorityClass::ForegroundInteractive,
      std::string("session-011")));
    assert_true(foreground_accepted.has_ticket(),
                "first foreground ticket should be accepted into empty queue");

    const auto foreground_rejected = foreground_scheduler.enqueue(make_ticket_request(
      "fg-2",
      "req-fg-2",
      SchedulerPriorityClass::ForegroundInteractive,
      std::string("session-011")));
    assert_true(!foreground_rejected.accepted,
                "second foreground ticket should be rejected when active queue depth is 1");
    assert_equal(
        std::string("RejectNew"),
        std::string(scheduler_overflow_disposition_name(
            foreground_rejected.applied_overflow_disposition.value())),
        "foreground overflow should surface reject-new disposition");
    assert_true(foreground_rejected.backpressure_state.rejecting_foreground,
                "foreground overflow should mark rejecting_foreground");
    assert_equal(
        std::string("ForegroundBusy"),
        std::string(scheduler_backpressure_signal_name(
            foreground_rejected.backpressure_state.dominant_signal)),
        "foreground overflow should publish busy backpressure signal");

    FakeScheduler recovery_scheduler;
    const auto recovery_accepted_1 = recovery_scheduler.enqueue(make_ticket_request(
      "recovery-1",
      "req-recovery-1",
      SchedulerPriorityClass::Recovery,
      std::nullopt,
      std::string("chk-011-a")));
    const auto recovery_accepted_2 = recovery_scheduler.enqueue(make_ticket_request(
      "recovery-2",
      "req-recovery-2",
      SchedulerPriorityClass::Recovery,
      std::nullopt,
      std::string("chk-011-b")));
    assert_true(recovery_accepted_1.has_ticket() && recovery_accepted_2.has_ticket(),
                "recovery queue should accept tickets up to configured limit");

    const auto recovery_rejected = recovery_scheduler.enqueue(make_ticket_request(
      "recovery-3",
      "req-recovery-3",
      SchedulerPriorityClass::Recovery,
      std::nullopt,
      std::string("chk-011-c")));
    assert_true(!recovery_rejected.accepted,
                "recovery queue overflow should reject additional ticket");
    assert_equal(
        std::string("EnterFailedSafe"),
        std::string(scheduler_overflow_disposition_name(
            recovery_rejected.applied_overflow_disposition.value())),
        "recovery queue overflow should recommend FailedSafe disposition");
    assert_true(recovery_rejected.backpressure_state.failed_safe_recommended,
                "recovery overflow should surface failed-safe recommendation");

    FakeScheduler maintenance_scheduler;
    const auto maintenance_accepted_1 = maintenance_scheduler.enqueue(make_ticket_request(
      "maintenance-1",
      "req-maintenance-1",
      SchedulerPriorityClass::Maintenance,
      std::nullopt,
      std::nullopt,
      std::string("maintenance")));
    const auto maintenance_accepted_2 = maintenance_scheduler.enqueue(make_ticket_request(
      "maintenance-2",
      "req-maintenance-2",
      SchedulerPriorityClass::Maintenance,
      std::nullopt,
      std::nullopt,
      std::string("maintenance")));
    assert_true(maintenance_accepted_1.has_ticket() && maintenance_accepted_2.has_ticket(),
                "maintenance queue should accept tickets up to local test limit");

    const auto maintenance_drop_oldest = maintenance_scheduler.enqueue(make_ticket_request(
      "maintenance-3",
      "req-maintenance-3",
      SchedulerPriorityClass::Maintenance,
      std::nullopt,
      std::nullopt,
      std::string("maintenance")));
    assert_true(maintenance_drop_oldest.accepted,
                "maintenance queue overflow should still accept latest ticket");
    assert_equal(
        std::string("DropOldest"),
        std::string(scheduler_overflow_disposition_name(
            maintenance_drop_oldest.applied_overflow_disposition.value())),
        "maintenance overflow should surface drop-oldest disposition");
    assert_true(maintenance_drop_oldest.backpressure_state.dropping_oldest_maintenance,
                "maintenance overflow should report drop-oldest backpressure");

    const auto maintenance_acquired = maintenance_scheduler.acquire_worker(
        AcquireWorkerRequest{
            .worker_budget = WorkerLeaseBudget{.max_workers = 1, .busy_workers = 0},
            .preferred_priority_class = SchedulerPriorityClass::Maintenance,
            .preferred_ticket_id = std::nullopt,
        });
    assert_true(maintenance_acquired.has_ticket(),
                "maintenance queue should provide a ticket for worker acquisition");
    assert_equal(
        std::string("maintenance-2"),
        maintenance_acquired.ticket->ticket_id,
        "drop-oldest policy should preserve the second ticket as oldest remaining item");
    assert_true(maintenance_acquired.ticket->has_worker_assignment(),
                "acquired ticket should bind a worker id");

    FakeScheduler cancellation_scheduler;
    SchedulerTicketRequest cancellation_request = make_ticket_request(
      "fg-cancel",
      "req-cancel",
      SchedulerPriorityClass::ForegroundInteractive,
      std::string("session-011-cancel"));
    const auto cancellation_enqueue = cancellation_scheduler.enqueue(cancellation_request);
    assert_true(cancellation_enqueue.has_ticket(),
                "foreground ticket for cancellation binding should be accepted");

    cancellation_request.cancellation_token.cancel();
    const auto cancellation_acquired = cancellation_scheduler.acquire_worker(
        AcquireWorkerRequest{
            .worker_budget = WorkerLeaseBudget{.max_workers = 1, .busy_workers = 0},
            .preferred_priority_class = SchedulerPriorityClass::ForegroundInteractive,
            .preferred_ticket_id = std::string("fg-cancel"),
        });
    assert_true(cancellation_acquired.has_ticket(),
                "acquire_worker should return the queued foreground ticket");
    assert_true(cancellation_acquired.ticket->cancellation_token.is_cancelled(),
                "worker ticket should observe cancellation token bound at enqueue time");

    const auto saturated_worker = cancellation_scheduler.acquire_worker(
        AcquireWorkerRequest{
            .worker_budget = WorkerLeaseBudget{.max_workers = 1, .busy_workers = 1},
            .preferred_priority_class = std::nullopt,
            .preferred_ticket_id = std::nullopt,
        });
    assert_true(!saturated_worker.acquired,
                "worker acquisition should reject when worker budget has no free capacity");
    assert_equal(
        std::string("WorkerPoolSaturated"),
        std::string(scheduler_backpressure_signal_name(
            saturated_worker.backpressure_state.dominant_signal)),
        "worker saturation should surface dedicated backpressure signal");

    const auto release_result = cancellation_scheduler.release_worker(
        ReleaseWorkerRequest{
            .ticket = *cancellation_acquired.ticket,
            .worker_completed = true,
        });
    assert_true(release_result.released,
                "release_worker should succeed for an acquired worker ticket");
    assert_true(!release_result.backpressure_state.workers_saturated(),
                "release_worker should restore worker budget capacity in fake scheduler");

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "SchedulerTest failed: " << ex.what() << '\n';
  } catch (...) {
    std::cerr << "SchedulerTest failed: unknown exception\n";
  }

  return 1;
}