#include <atomic>
#include <barrier>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "scheduling/Scheduler.h"
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

    dasall::runtime::Scheduler foreground_scheduler;
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

    dasall::runtime::Scheduler recovery_scheduler;
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

    dasall::runtime::Scheduler maintenance_scheduler(2, 2);
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

    dasall::runtime::Scheduler cancellation_scheduler;
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

    dasall::runtime::Scheduler stress_scheduler(2, 4);
    constexpr int kStressThreadCount = 4;
    constexpr int kStressIterations = 48;
    std::barrier sync_point(kStressThreadCount + 1);
    std::atomic<int> accepted_count{0};
    std::atomic<int> acquired_count{0};
    std::atomic<int> released_count{0};
    std::vector<std::thread> workers;
    workers.reserve(kStressThreadCount);

    for (int worker_index = 0; worker_index < kStressThreadCount; ++worker_index) {
      workers.emplace_back(
          [worker_index,
           &stress_scheduler,
           &sync_point,
           &accepted_count,
           &acquired_count,
           &released_count]() {
            sync_point.arrive_and_wait();
            for (int iteration = 0; iteration < kStressIterations; ++iteration) {
              const auto enqueue_result = stress_scheduler.enqueue(make_ticket_request(
                  "maintenance-stress-" + std::to_string(worker_index) + "-" +
                      std::to_string(iteration),
                  "req-maintenance-stress-" + std::to_string(worker_index) + "-" +
                      std::to_string(iteration),
                  SchedulerPriorityClass::Maintenance,
                  std::nullopt,
                  std::nullopt,
                  std::string("maintenance")));
              if (enqueue_result.accepted) {
                accepted_count.fetch_add(1, std::memory_order_relaxed);
              }

              const auto acquire_result = stress_scheduler.acquire_worker(
                  AcquireWorkerRequest{
                      .worker_budget = WorkerLeaseBudget{.max_workers = 1, .busy_workers = 0},
                      .preferred_priority_class = SchedulerPriorityClass::Maintenance,
                      .preferred_ticket_id = std::nullopt,
                  });
              if (!acquire_result.acquired || !acquire_result.ticket.has_value()) {
                (void)stress_scheduler.backpressure_state();
                continue;
              }

              acquired_count.fetch_add(1, std::memory_order_relaxed);
              const auto stress_release_result = stress_scheduler.release_worker(
                  ReleaseWorkerRequest{
                      .ticket = *acquire_result.ticket,
                      .worker_completed = true,
                  });
              if (stress_release_result.released) {
                released_count.fetch_add(1, std::memory_order_relaxed);
              }
            }
          });
    }

    sync_point.arrive_and_wait();
    for (auto& worker : workers) {
      worker.join();
    }

    const auto stress_state = stress_scheduler.backpressure_state();
    assert_true(accepted_count.load(std::memory_order_acquire) > 0,
                "scheduler stress test should accept work under concurrent enqueue");
    assert_true(acquired_count.load(std::memory_order_acquire) > 0,
                "scheduler stress test should acquire worker leases without deadlock");
    assert_true(released_count.load(std::memory_order_acquire) > 0,
                "scheduler stress test should release worker leases after acquisition");
    assert_true(stress_state.maintenance_queue_depth <= stress_state.maintenance_queue_limit,
                "scheduler stress test should keep maintenance queue depth bounded");
    assert_true(!stress_state.workers_saturated(),
                "scheduler stress test should leave worker leases unsaturated after joins");

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "SchedulerTest failed: " << ex.what() << '\n';
  } catch (...) {
    std::cerr << "SchedulerTest failed: unknown exception\n";
  }

  return 1;
}