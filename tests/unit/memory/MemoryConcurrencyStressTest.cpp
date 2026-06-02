#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#define private public
#include "MemoryManagerInternal.h"
#undef private

#include "support/TestAssertions.h"

namespace {

constexpr int kStressIterationCount = 1024;
constexpr int kStressWorkerCount = 3;

std::filesystem::path make_temp_database_path(const std::string& stem) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         (stem + "-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

template <typename Predicate>
void wait_until(Predicate&& predicate, int spin_limit) {
  for (int spin = 0; spin < spin_limit; ++spin) {
    if (predicate()) {
      return;
    }
    std::this_thread::yield();
  }
}

[[nodiscard]] bool has_warning(const std::vector<std::string>& warnings,
                               const std::string& warning_key) {
  return std::find(warnings.begin(), warnings.end(), warning_key) != warnings.end();
}

[[nodiscard]] std::string summarize_failures(
    const std::vector<std::string>& failures,
    const std::string& fallback_message) {
  if (failures.empty()) {
    return fallback_message;
  }

  std::ostringstream stream;
  stream << fallback_message;
  for (const auto& failure : failures) {
    stream << " | " << failure;
  }
  return stream.str();
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 1;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.storage.busy_timeout_ms = 100;
  config.context.compression_trigger_turns = 4;
  config.context.compression_trigger_ratio = 0.5;
  config.maintenance.retention_turns = 96;
  config.maintenance.fact_ttl_ms = 1000;
  config.maintenance.experience_ttl_ms = 1000;
  config.maintenance.quarantine_ttl_ms = 1000;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_writeback_request(
    int iteration) {
  const auto now_millis = current_time_millis();
  const bool service_enabled = (iteration % 2) == 0;
  const std::string session_id = "session-memory-concurrency-stress";
  const std::string turn_id = "turn-memory-concurrency-stress-" +
                              std::to_string(iteration);
  const std::string fact_text =
      service_enabled ? "service mode enabled" : "service mode disabled";

  dasall::memory::MemoryWritebackRequest request;
  request.request_id = "writeback-memory-concurrency-stress-" +
                       std::to_string(iteration);
  request.session_id = session_id;
  request.trace_id = "trace-memory-concurrency-stress";
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = "capture stress iteration " + std::to_string(iteration);
  request.turn.agent_response = fact_text;
  request.turn.created_at = now_millis + iteration;

  if ((iteration % 32) == 0) {
    request.summary_candidate = dasall::contracts::SummaryMemory{};
    request.summary_candidate->summary_text = "summary for " + turn_id;
    request.summary_candidate->source_turn_ids = std::vector<std::string>{turn_id};
    request.summary_candidate->confirmed_facts = std::vector<std::string>{fact_text};
    request.summary_candidate->created_at = now_millis + iteration;
  }

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "service_state";
  fact_candidate.fact.confidence_score = 90 + (iteration % 5);
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.fact.created_at = now_millis + iteration;
  fact_candidate.extraction_source = "stress";
  request.fact_candidates.push_back(std::move(fact_candidate));

  if ((iteration % 16) == 0) {
    dasall::memory::ExperienceCandidate experience_candidate;
    experience_candidate.experience.lesson_summary =
        "serialize writeback and maintenance on one writer mutex";
    experience_candidate.experience.trigger_condition =
        "memory concurrency stress iteration";
    experience_candidate.experience.recommended_action =
        "preserve the shared writer lock ordering";
    experience_candidate.experience.created_at = now_millis + iteration;
    experience_candidate.extraction_source = "stress";
    request.experience_candidates.push_back(std::move(experience_candidate));
  }

  return request;
}

[[nodiscard]] dasall::memory::MemoryContextRequest make_context_request(
    int iteration) {
  dasall::memory::MemoryContextRequest request;
  request.request_id = "context-memory-concurrency-stress-" +
                       std::to_string(iteration);
  request.session_id = "session-memory-concurrency-stress";
  request.trace_id = "trace-memory-concurrency-stress";
  request.stage = "reasoning";
  request.user_turn = "parallel prepare_context iteration " + std::to_string(iteration);
  request.goal_summary = "validate memory concurrency stress";
  request.constraints_summary =
      "write_back and run_maintenance must share one writer serialization gate";
  request.latest_observation_digest_summary =
      "stress loop is exercising prepare_context, write_back and maintenance";
  request.visible_tools = {"shell", "ctest"};
  request.token_budget_hint = 256;
  request.latency_budget_ms = 50;
  request.external_evidence = {"memory concurrency stress"};
  return request;
}

void assert_shared_writer_mutex_wiring(dasall::memory::IMemoryManager& manager) {
  using dasall::tests::support::assert_true;

  auto* implementation = dynamic_cast<dasall::memory::MemoryManager*>(&manager);
  assert_true(implementation != nullptr,
              "memory concurrency stress requires access to the concrete manager implementation");
  assert_true(implementation->dependencies_.store_writer_mutex != nullptr,
              "memory concurrency stress requires a shared store writer mutex");
  assert_true(implementation->dependencies_.writeback_coordinator != nullptr,
              "memory concurrency stress requires a wired writeback coordinator");
  assert_true(implementation->dependencies_.maintenance_worker != nullptr,
              "memory concurrency stress requires a wired maintenance worker");
  assert_true(
      implementation->dependencies_.writeback_coordinator->writer_mutex_.get() ==
          implementation->dependencies_.maintenance_worker->writer_mutex_.get(),
      "writeback and maintenance must share the same writer mutex to preserve lock ordering");
  assert_true(
      implementation->dependencies_.writeback_coordinator->writer_mutex_.get() ==
          implementation->dependencies_.store_writer_mutex.get(),
      "the concrete manager should wire writeback through the shared writer mutex owner");
}

void seed_initial_session(dasall::memory::IMemoryManager& manager) {
  using dasall::tests::support::assert_true;

  for (int iteration = 0; iteration < 2; ++iteration) {
    const auto result = manager.write_back(make_writeback_request(iteration));
    assert_true(!result.result_code.has_value(),
                "memory concurrency stress should seed the session before parallel workers start");
  }
}

void test_memory_manager_handles_parallel_prepare_writeback_and_maintenance() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-concurrency-stress");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error(
        "DASALL_SQL_MEMORY_DIR must be defined for memory concurrency stress coverage");
  }

  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_equal(0, static_cast<int>(init_code),
               "memory concurrency stress requires a sqlite-backed manager");
  assert_shared_writer_mutex_wiring(*manager);
  seed_initial_session(*manager);

  std::atomic<int> ready_count = 0;
  std::atomic<bool> start_workers = false;
  std::atomic<int> context_iterations = 0;
  std::atomic<int> writeback_iterations = 0;
  std::atomic<int> maintenance_iterations = 0;
  std::atomic<int> failure_count = 0;
  std::mutex failure_mutex;
  std::vector<std::string> failures;

  const auto record_failure = [&](const std::string& message) {
    failure_count.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> lock(failure_mutex);
    if (failures.size() < 8U) {
      failures.push_back(message);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(kStressWorkerCount);

  workers.emplace_back([&]() {
    ready_count.fetch_add(1, std::memory_order_acq_rel);
    wait_until(
        [&]() { return start_workers.load(std::memory_order_acquire); },
        20000);

    for (int iteration = 0; iteration < kStressIterationCount; ++iteration) {
      const auto request = make_context_request(iteration);
      const auto result = manager->prepare_context(request);
      if (result.result_code.has_value()) {
        record_failure("prepare_context returned result_code at iteration " +
                       std::to_string(iteration));
      }
      if (result.context_packet.request_id != request.request_id) {
        record_failure("prepare_context lost request_id echo at iteration " +
                       std::to_string(iteration));
      }
      if (!result.context_packet.active_tools.has_value() ||
          result.context_packet.active_tools->size() != 2U) {
        record_failure("prepare_context lost active tool projection at iteration " +
                       std::to_string(iteration));
      }
      context_iterations.fetch_add(1, std::memory_order_acq_rel);
    }
  });

  workers.emplace_back([&]() {
    ready_count.fetch_add(1, std::memory_order_acq_rel);
    wait_until(
        [&]() { return start_workers.load(std::memory_order_acquire); },
        20000);

    for (int iteration = 2; iteration < kStressIterationCount + 2; ++iteration) {
      const auto result = manager->write_back(make_writeback_request(iteration));
      if (result.result_code.has_value()) {
        record_failure("write_back returned result_code at iteration " +
                       std::to_string(iteration));
      }
      if (!result.persisted_turn_id.has_value()) {
        record_failure("write_back did not persist a turn id at iteration " +
                       std::to_string(iteration));
      }
      writeback_iterations.fetch_add(1, std::memory_order_acq_rel);
    }
  });

  workers.emplace_back([&]() {
    ready_count.fetch_add(1, std::memory_order_acq_rel);
    wait_until(
        [&]() { return start_workers.load(std::memory_order_acquire); },
        20000);

    for (int iteration = 0; iteration < kStressIterationCount; ++iteration) {
      dasall::memory::MaintenanceRequest request;
      request.request_id = "maintenance-memory-concurrency-stress-" +
                           std::to_string(iteration);
      request.trace_id = "trace-memory-concurrency-stress";
      request.run_checkpoint = true;
      request.run_retention = true;
      request.run_quarantine_cleanup = true;
      request.run_vector_rebuild = false;

      const auto report = manager->run_maintenance(request);
      if (has_warning(report.warnings, "maintenance_store_not_open") ||
          has_warning(report.warnings, "turn_retention_failed") ||
          has_warning(report.warnings, "turn_retention_commit_failed") ||
          has_warning(report.warnings, "fact_retention_failed") ||
          has_warning(report.warnings, "experience_retention_failed") ||
          has_warning(report.warnings, "quarantine_cleanup_failed")) {
        record_failure("run_maintenance surfaced a storage failure warning at iteration " +
                       std::to_string(iteration));
      }
      maintenance_iterations.fetch_add(1, std::memory_order_acq_rel);
    }
  });

  wait_until(
      [&]() {
        return ready_count.load(std::memory_order_acquire) == kStressWorkerCount;
      },
      40000);
  assert_equal(kStressWorkerCount, ready_count.load(std::memory_order_acquire),
               "all concurrency stress workers should be ready before the loop starts");

  start_workers.store(true, std::memory_order_release);
  for (auto& worker : workers) {
    worker.join();
  }

  assert_equal(kStressIterationCount,
               context_iterations.load(std::memory_order_acquire),
               "prepare_context worker should complete the full stress iteration count");
  assert_equal(kStressIterationCount,
               writeback_iterations.load(std::memory_order_acquire),
               "write_back worker should complete the full stress iteration count");
  assert_equal(kStressIterationCount,
               maintenance_iterations.load(std::memory_order_acquire),
               "run_maintenance worker should complete the full stress iteration count");
  assert_true(failure_count.load(std::memory_order_acquire) == 0,
              summarize_failures(failures,
                                 "memory concurrency stress should complete without concurrency failures"));

  const auto export_result = manager->export_working_memory_snapshot(
      dasall::memory::WorkingMemoryExportRequest{
          .session_id = "session-memory-concurrency-stress",
          .export_reason = "stress-verify",
          .include_ephemeral_facts = false,
      });
  assert_true(!export_result.result_code.has_value(),
              "memory concurrency stress should still export a working snapshot after the loop");
  assert_true(
      std::any_of(export_result.snapshot.slots.begin(),
                  export_result.snapshot.slots.end(),
                  [](const dasall::memory::WorkingMemorySlot& slot) {
                    return slot.key == "latest_turn_id" && !slot.value.empty();
                  }),
      "memory concurrency stress should keep the latest_turn_id working-memory slot coherent");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_manager_handles_parallel_prepare_writeback_and_maintenance();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}