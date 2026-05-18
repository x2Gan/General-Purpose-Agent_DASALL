#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include <sqlite3.h>

#define private public
#include "store/sqlite/SqliteMemoryStore.h"
#undef private

#include "support/TestAssertions.h"
#include <functional>

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

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

dasall::memory::MemoryConfig make_sqlite_config(const std::filesystem::path& database_path,
                                                int reader_pool_size) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = reader_pool_size;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  return config;
}

void wait_until(const std::function<bool()>& predicate, int spin_limit) {
  for (int spin = 0; spin < spin_limit; ++spin) {
    if (predicate()) {
      return;
    }
    std::this_thread::yield();
  }
}

void test_sqlite_memory_store_reader_lease_serializes_pool_reuse() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-store-reader-lease");
  cleanup_database_artifacts(database_path);

  dasall::memory::store::sqlite::SqliteMemoryStore store;
  const auto config = make_sqlite_config(database_path, 1);
  assert_true(!store.open(config).has_value(),
              "sqlite store should open before reader lease concurrency coverage");

  auto first_lease = store.select_reader_connection();
  assert_true(first_lease.connection != nullptr,
              "reader lease coverage requires one open reader connection");

  std::atomic<bool> contender_started = false;
  std::atomic<bool> contender_acquired = false;
  std::thread contender([&store, &contender_started, &contender_acquired]() {
    contender_started.store(true, std::memory_order_release);
    auto second_lease = store.select_reader_connection();
    contender_acquired.store(second_lease.connection != nullptr,
                             std::memory_order_release);
  });

  wait_until(
      [&contender_started]() {
        return contender_started.load(std::memory_order_acquire);
      },
      20000);
  assert_true(contender_started.load(std::memory_order_acquire),
              "contender thread should start before lease blocking is evaluated");

  wait_until(
      [&contender_acquired]() {
        return contender_acquired.load(std::memory_order_acquire);
      },
      20000);
  assert_true(!contender_acquired.load(std::memory_order_acquire),
              "the only reader connection should stay leased until the first borrower releases it");

  first_lease = {};
  contender.join();

  assert_true(contender_acquired.load(std::memory_order_acquire),
              "the contender should acquire the reader connection after the first lease is released");

  store.close();
  cleanup_database_artifacts(database_path);
}

void test_sqlite_memory_store_public_reads_stay_stable_under_parallel_queries() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-store-parallel-queries");
  cleanup_database_artifacts(database_path);

  dasall::memory::store::sqlite::SqliteMemoryStore store;
  const auto config = make_sqlite_config(database_path, 1);
  const auto now_millis = current_time_millis();
  assert_true(!store.open(config).has_value(),
              "sqlite store should open before public read concurrency coverage");

  dasall::contracts::Session session;
  session.session_id = "session-sqlite-concurrency";
  session.user_id = "user-sqlite-concurrency";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = now_millis - 1000;
  session.last_active_at = now_millis - 900;
  assert_true(store.create_session(session).ok,
              "public read concurrency coverage requires a persisted session");

  dasall::contracts::Turn turn;
  turn.turn_id = "turn-sqlite-concurrency-001";
  turn.session_id = *session.session_id;
  turn.user_input = "capture reader pool behavior";
  turn.agent_response = "reader pool should serialize reused sqlite handles";
  turn.created_at = now_millis - 800;
  assert_true(store.append_turn(turn).ok,
              "public read concurrency coverage requires a persisted turn");

  dasall::contracts::SummaryMemory summary;
  summary.summary_id = "summary-sqlite-concurrency-001";
  summary.session_id = *session.session_id;
  summary.summary_text = "reader lease summary";
  summary.source_turn_ids = std::vector<std::string>{*turn.turn_id};
  summary.created_at = now_millis - 700;
  assert_true(store.upsert_summary(summary).ok,
              "public read concurrency coverage requires a persisted summary");

  dasall::contracts::MemoryFact fact;
  fact.fact_id = "fact-sqlite-concurrency-001";
  fact.session_id = *session.session_id;
  fact.fact_text = "reader pool lease is active";
  fact.source_turn_ids = std::vector<std::string>{*turn.turn_id};
  fact.confidence_score = 99;
  fact.fact_type = "status";
  fact.created_at = now_millis - 600;
  assert_true(store.insert_fact(fact).ok,
              "public read concurrency coverage requires a persisted fact");

  dasall::contracts::ExperienceMemory experience;
  experience.experience_id = "experience-sqlite-concurrency-001";
  experience.session_id = *session.session_id;
  experience.lesson_summary = "parallel reads should remain stable";
  experience.trigger_condition = "prepare_context_parallel";
  experience.recommended_action = "serialize reader lease per connection";
  experience.source_turn_ids = std::vector<std::string>{*turn.turn_id};
  experience.created_at = now_millis - 500;
  experience.expires_at = now_millis + 3600000;
  experience.tags = std::vector<std::string>{"stage:reasoning"};
  assert_true(store.insert_experience(experience).ok,
              "public read concurrency coverage requires a persisted experience");

  constexpr int thread_count = 4;
  constexpr int iterations_per_thread = 32;
  std::atomic<int> ready_count = 0;
  std::atomic<bool> start_queries = false;
  std::atomic<int> failure_count = 0;
  std::vector<std::thread> readers;
  readers.reserve(thread_count);

  for (int thread_index = 0; thread_index < thread_count; ++thread_index) {
    readers.emplace_back([&store, &ready_count, &start_queries, &failure_count]() {
      ready_count.fetch_add(1, std::memory_order_acq_rel);
      wait_until(
          [&start_queries]() {
            return start_queries.load(std::memory_order_acquire);
          },
          20000);

      for (int iteration = 0; iteration < iterations_per_thread; ++iteration) {
        const auto bundle = store.load_session_bundle(dasall::memory::SessionLoadRequest{
            .session_id = "session-sqlite-concurrency",
            .recent_turn_limit = 1,
        });
        if (bundle.total_turn_count != 1 || bundle.recent_turns.size() != 1U) {
          failure_count.fetch_add(1, std::memory_order_acq_rel);
        }

        const auto latest_summary =
            store.load_latest_summary("session-sqlite-concurrency");
        if (!latest_summary.has_value() ||
            latest_summary->summary_id !=
                std::optional<std::string>{"summary-sqlite-concurrency-001"}) {
          failure_count.fetch_add(1, std::memory_order_acq_rel);
        }

        const auto facts = store.query_facts(dasall::memory::FactQuery{
            .session_id = std::optional<std::string>{"session-sqlite-concurrency"},
            .user_id = std::optional<std::string>{"user-sqlite-concurrency"},
            .fact_type = std::optional<std::string>{"status"},
            .min_confidence = 1,
            .exclude_superseded = true,
            .limit = 4,
        });
        if (facts.total_count != 1) {
          failure_count.fetch_add(1, std::memory_order_acq_rel);
        }

        const auto experiences = store.query_experiences(dasall::memory::ExperienceQuery{
            .session_id = std::optional<std::string>{"session-sqlite-concurrency"},
            .user_id = std::optional<std::string>{"user-sqlite-concurrency"},
            .stage = std::optional<std::string>{"reasoning"},
            .exclude_expired = true,
            .limit = 4,
        });
        if (experiences.total_count != 1) {
          failure_count.fetch_add(1, std::memory_order_acq_rel);
        }

        if (store.count_turns("session-sqlite-concurrency") != 1) {
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
               "all reader threads should be ready before parallel queries start");

  start_queries.store(true, std::memory_order_release);
  for (auto& reader : readers) {
    reader.join();
  }

  assert_equal(0, failure_count.load(std::memory_order_acquire),
               "parallel sqlite reads should keep returning the persisted session bundle, summary, fact and experience rows");

  store.close();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_sqlite_memory_store_reader_lease_serializes_pool_reuse();
    test_sqlite_memory_store_public_reads_stay_stable_under_parallel_queries();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}