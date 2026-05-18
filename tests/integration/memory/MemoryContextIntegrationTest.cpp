#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for memory context integration coverage
#endif

#include "IMemoryManager.h"
#include "support/TestAssertions.h"

namespace {

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

dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 1;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.compression_trigger_turns = 2;
  config.context.compression_trigger_ratio = 0.5;
  config.vector.enabled = false;
  return config;
}

dasall::memory::MemoryWritebackRequest make_request(
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& summary_text,
    const std::string& fact_text,
    std::uint32_t confidence_score) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = session_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = summary_text;
  request.summary_candidate->confirmed_facts = std::vector<std::string>{fact_text};

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = confidence_score;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "turn";
  request.fact_candidates.push_back(std::move(fact_candidate));
  return request;
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

void test_memory_context_prepare_context_stays_stable_under_parallel_queries() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-context-concurrency");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "memory manager should initialize before parallel context coverage");

  const auto first_writeback = manager->write_back(make_request(
      "session-memory-context-concurrency", "turn-memory-context-concurrency-001",
      "seed first context turn", "first writeback is complete",
      "first context summary", "context summary was persisted", 85));
  assert_true(!first_writeback.result_code.has_value(),
              "parallel context coverage requires the first writeback to succeed");

  const auto second_writeback = manager->write_back(make_request(
      "session-memory-context-concurrency", "turn-memory-context-concurrency-002",
      "seed second context turn", "second writeback is complete",
      "second context summary", "parallel context query should reuse leased reader connections",
      93));
  assert_true(!second_writeback.result_code.has_value(),
              "parallel context coverage requires the second writeback to succeed");

  constexpr int thread_count = 4;
  constexpr int iterations_per_thread = 16;
  std::atomic<int> ready_count = 0;
  std::atomic<bool> start_queries = false;
  std::atomic<int> failure_count = 0;
  std::vector<std::thread> readers;
  readers.reserve(thread_count);

  for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
    readers.emplace_back([manager_ptr = manager.get(),
                          &ready_count,
                          &start_queries,
                          &failure_count,
                          thread_index]() {
      ready_count.fetch_add(1, std::memory_order_acq_rel);
      wait_until(
          [&start_queries]() {
            return start_queries.load(std::memory_order_acquire);
          },
          20000);

      for (int iteration = 0; iteration < iterations_per_thread; ++iteration) {
        const auto context_result = manager_ptr->prepare_context(
            dasall::memory::MemoryContextRequest{
                .request_id = "req-memory-context-concurrency-" +
                              std::to_string(thread_index) + "-" +
                              std::to_string(iteration),
                .session_id = "session-memory-context-concurrency",
                .stage = "reasoning",
                .goal_summary = "validate parallel prepare_context",
                .constraints_summary = "reader pool lease must serialize reused sqlite handles",
                .latest_observation_digest_summary =
                    "parallel reads should remain stable in build-ci",
                .visible_tools = {"shell", "ctest"},
                .token_budget_hint = 256,
                .latency_budget_ms = 100,
                .external_evidence = {"local build-ci concurrency gate"},
                .retrieval_evidence_refs = {},
            });

        if (context_result.result_code.has_value() || context_result.degraded ||
            !context_result.context_packet.summary_memory.has_value() ||
            !context_result.context_packet.belief_state_summary.has_value() ||
            !context_result.context_packet.active_tools.has_value() ||
            context_result.context_packet.active_tools->size() != 2U) {
          failure_count.fetch_add(1, std::memory_order_acq_rel);
        }
      }
    });
  }

  wait_until(
      [&ready_count]() {
        return ready_count.load(std::memory_order_acquire) == thread_count;
      },
      20000);
  assert_equal(thread_count, ready_count.load(std::memory_order_acquire),
               "all context reader threads should be ready before concurrent prepare_context starts");

  start_queries.store(true, std::memory_order_release);
  for (auto& reader : readers) {
    reader.join();
  }

  assert_equal(0, failure_count.load(std::memory_order_acquire),
               "parallel prepare_context should stay stable with a single leased sqlite reader connection");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_context_prepare_context_stays_stable_under_parallel_queries();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}