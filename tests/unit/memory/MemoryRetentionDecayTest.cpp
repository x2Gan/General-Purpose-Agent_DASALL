#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <sqlite3.h>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR "/home/gangan/DASALL/sql/memory"
#endif

#include "config/MemoryConfig.h"
#include "context/CandidateCollector.h"
#include "maintenance/MemoryMaintenanceWorker.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"
#include "working/IWorkingMemoryBoard.h"

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

std::int64_t query_int64(const std::filesystem::path& database_path,
                         const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for int64 query");
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(connection);
    throw std::runtime_error("failed to prepare sqlite int64 query");
  }

  std::int64_t value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int64(statement, 0);
  }

  sqlite3_finalize(statement);
  sqlite3_close(connection);
  return value;
}

int query_count(const std::filesystem::path& database_path,
                const std::string& sql) {
  sqlite3* connection = nullptr;
  if (sqlite3_open(database_path.string().c_str(), &connection) != SQLITE_OK) {
    throw std::runtime_error("failed to open sqlite connection for count query");
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    sqlite3_close(connection);
    throw std::runtime_error("failed to prepare sqlite count query");
  }

  int value = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    value = sqlite3_column_int(statement, 0);
  }

  sqlite3_finalize(statement);
  sqlite3_close(connection);
  return value;
}

dasall::memory::MemoryConfig make_decay_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.recent_turn_limit = 2;
  config.context.fact_confidence_floor = 0;
  config.maintenance.fact_ttl_ms = 0;
  config.maintenance.experience_ttl_ms = 0;
  config.maintenance.decay.enabled = true;
  config.maintenance.decay.time_constant_ms = 1000.0;
  config.maintenance.decay.minimum_score = 0.2;
  config.maintenance.decay.minimum_age_ms = 1000;
  return config;
}

void seed_session(dasall::memory::IMemoryStore& store,
                  const std::string& session_id,
                  const std::string& user_id,
                  std::int64_t created_at) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.turn_ids = std::vector<std::string>{};
  session.user_id = user_id;
  session.created_at = created_at;
  session.last_active_at = created_at;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed decay session");
  }
}

void seed_turn(dasall::memory::IMemoryStore& store,
               const std::string& turn_id,
               const std::string& session_id,
               std::int64_t created_at) {
  dasall::contracts::Turn turn;
  turn.turn_id = turn_id;
  turn.session_id = session_id;
  turn.user_input = "retention decay seed";
  turn.created_at = created_at;
  if (!store.append_turn(turn).ok) {
    throw std::runtime_error("failed to seed decay turn");
  }
}

void test_candidate_collector_prefers_hot_records_and_touches_access_metadata() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-retention-decay-ranking");
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_decay_config(database_path);
  const auto now_millis = current_time_millis();

  assert_true(!store->open(config).has_value(),
              "sqlite store should open for retention decay ranking coverage");

  seed_session(*store, "session-decay-ranking", "user-decay-ranking", now_millis - 20000);
  seed_turn(*store, "turn-decay-ranking", "session-decay-ranking", now_millis - 19000);

  dasall::contracts::MemoryFact hot_fact;
  hot_fact.fact_id = "fact-hot";
  hot_fact.session_id = "session-decay-ranking";
  hot_fact.fact_text = "热事实应该优先于更新但冷的事实。";
  hot_fact.source_turn_ids = std::vector<std::string>{"turn-decay-ranking"};
  hot_fact.confidence_score = 80;
  hot_fact.created_at = now_millis - 15000;
  hot_fact.fact_type = "preference";
  assert_true(store->insert_fact(hot_fact).ok,
              "ranking coverage should insert the hot fact");

  dasall::contracts::MemoryFact cold_fact;
  cold_fact.fact_id = "fact-cold";
  cold_fact.session_id = "session-decay-ranking";
  cold_fact.fact_text = "冷事实不应因为创建时间更新就长期压过热事实。";
  cold_fact.source_turn_ids = std::vector<std::string>{"turn-decay-ranking"};
  cold_fact.confidence_score = 80;
  cold_fact.created_at = now_millis - 2000;
  cold_fact.fact_type = "preference";
  assert_true(store->insert_fact(cold_fact).ok,
              "ranking coverage should insert the cold fact");

  dasall::contracts::ExperienceMemory hot_experience;
  hot_experience.experience_id = "experience-hot";
  hot_experience.session_id = "session-decay-ranking";
  hot_experience.lesson_summary = "热经验应该优先于冷经验。";
  hot_experience.trigger_condition = "stage=plan";
  hot_experience.recommended_action = "prefer hot experience";
  hot_experience.created_at = now_millis - 15000;
  hot_experience.effectiveness_score = 70;
  hot_experience.tags = std::vector<std::string>{"stage:plan"};
  assert_true(store->insert_experience(hot_experience).ok,
              "ranking coverage should insert the hot experience");

  dasall::contracts::ExperienceMemory cold_experience;
  cold_experience.experience_id = "experience-cold";
  cold_experience.session_id = "session-decay-ranking";
  cold_experience.lesson_summary = "冷经验不应长期压过热经验。";
  cold_experience.trigger_condition = "stage=plan";
  cold_experience.recommended_action = "deprioritize cold experience";
  cold_experience.created_at = now_millis - 2000;
  cold_experience.effectiveness_score = 70;
  cold_experience.tags = std::vector<std::string>{"stage:plan"};
  assert_true(store->insert_experience(cold_experience).ok,
              "ranking coverage should insert the cold experience");

  assert_true(store->touch_facts({"fact-hot"}, now_millis - 100).ok,
              "ranking coverage should warm the hot fact before collection");
  assert_true(store->touch_experiences({"experience-hot"}, now_millis - 100).ok,
              "ranking coverage should warm the hot experience before collection");

  const auto hot_fact_hits_before = query_int64(
      database_path, "SELECT hit_count FROM facts WHERE fact_id = 'fact-hot'");
  const auto cold_fact_hits_before = query_int64(
      database_path, "SELECT hit_count FROM facts WHERE fact_id = 'fact-cold'");
  const auto hot_experience_hits_before = query_int64(
      database_path,
      "SELECT hit_count FROM experiences WHERE experience_id = 'experience-hot'");
  const auto cold_experience_hits_before = query_int64(
      database_path,
      "SELECT hit_count FROM experiences WHERE experience_id = 'experience-cold'");

  auto board = dasall::memory::create_working_memory_board();
  dasall::memory::CandidateCollector collector(
      *board, *store, *store, *store, *store, config, nullptr);

  const auto set = collector.collect(dasall::memory::CandidateCollectRequest{
      .session_id = "session-decay-ranking",
      .stage = "plan",
      .goal_summary = "验证 retention decay 排序",
      .token_budget_hint = 512,
      .latency_budget_ms = 100,
      .external_evidence = {},
  });

  assert_equal(2, static_cast<int>(set.relevant_facts.size()),
               "collector should keep both facts when confidence floor allows them");
  assert_true(set.relevant_facts.front().fact_id == std::optional<std::string>{"fact-hot"},
              "collector should prefer the older but recently accessed hot fact over the newer cold fact");
  assert_equal(2, static_cast<int>(set.relevant_experiences.size()),
               "collector should keep both stage-matching experiences");
  assert_true(set.relevant_experiences.front().experience_id ==
                  std::optional<std::string>{"experience-hot"},
              "collector should prefer the older but recently accessed hot experience over the newer cold experience");

  assert_equal(hot_fact_hits_before + 1,
               query_int64(database_path,
                           "SELECT hit_count FROM facts WHERE fact_id = 'fact-hot'"),
               "collector should touch hot facts after retrieval");
  assert_equal(cold_fact_hits_before + 1,
               query_int64(database_path,
                           "SELECT hit_count FROM facts WHERE fact_id = 'fact-cold'"),
               "collector should touch cold facts that were returned in the result set");
  assert_equal(hot_experience_hits_before + 1,
               query_int64(database_path,
                           "SELECT hit_count FROM experiences WHERE experience_id = 'experience-hot'"),
               "collector should touch hot experiences after retrieval");
  assert_equal(cold_experience_hits_before + 1,
               query_int64(database_path,
                           "SELECT hit_count FROM experiences WHERE experience_id = 'experience-cold'"),
               "collector should touch cold experiences that were returned in the result set");

  store->close();
  cleanup_database_artifacts(database_path);
}

void test_maintenance_purges_old_cold_records_by_decay() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-retention-decay-purge");
  cleanup_database_artifacts(database_path);

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  auto config = make_decay_config(database_path);
  const auto now_millis = current_time_millis();

  assert_true(!store->open(config).has_value(),
              "sqlite store should open for retention decay purge coverage");

  seed_session(*store, "session-decay-purge", "user-decay-purge", now_millis - 25000);
  seed_turn(*store, "turn-decay-purge", "session-decay-purge", now_millis - 24000);

  dasall::contracts::MemoryFact hot_fact;
  hot_fact.fact_id = "fact-purge-hot";
  hot_fact.session_id = "session-decay-purge";
  hot_fact.fact_text = "recently accessed fact should stay";
  hot_fact.source_turn_ids = std::vector<std::string>{"turn-decay-purge"};
  hot_fact.confidence_score = 85;
  hot_fact.created_at = now_millis - 20000;
  hot_fact.fact_type = "state";
  hot_fact.valid_until = now_millis + 600000;
  assert_true(store->insert_fact(hot_fact).ok,
              "purge coverage should insert the hot fact");

  dasall::contracts::MemoryFact cold_fact;
  cold_fact.fact_id = "fact-purge-cold";
  cold_fact.session_id = "session-decay-purge";
  cold_fact.fact_text = "cold fact should be forgotten";
  cold_fact.source_turn_ids = std::vector<std::string>{"turn-decay-purge"};
  cold_fact.confidence_score = 85;
  cold_fact.created_at = now_millis - 20000;
  cold_fact.fact_type = "state";
  cold_fact.valid_until = now_millis + 600000;
  assert_true(store->insert_fact(cold_fact).ok,
              "purge coverage should insert the cold fact");

  dasall::contracts::ExperienceMemory hot_experience;
  hot_experience.experience_id = "experience-purge-hot";
  hot_experience.session_id = "session-decay-purge";
  hot_experience.lesson_summary = "recently accessed experience should stay";
  hot_experience.trigger_condition = "stage=plan";
  hot_experience.recommended_action = "keep hot experience";
  hot_experience.created_at = now_millis - 20000;
  hot_experience.expires_at = now_millis + 600000;
  hot_experience.effectiveness_score = 75;
  hot_experience.tags = std::vector<std::string>{"stage:plan"};
  assert_true(store->insert_experience(hot_experience).ok,
              "purge coverage should insert the hot experience");

  dasall::contracts::ExperienceMemory cold_experience;
  cold_experience.experience_id = "experience-purge-cold";
  cold_experience.session_id = "session-decay-purge";
  cold_experience.lesson_summary = "cold experience should be forgotten";
  cold_experience.trigger_condition = "stage=plan";
  cold_experience.recommended_action = "purge cold experience";
  cold_experience.created_at = now_millis - 20000;
  cold_experience.expires_at = now_millis + 600000;
  cold_experience.effectiveness_score = 75;
  cold_experience.tags = std::vector<std::string>{"stage:plan"};
  assert_true(store->insert_experience(cold_experience).ok,
              "purge coverage should insert the cold experience");

  assert_true(store->touch_facts({"fact-purge-hot"}, now_millis - 100).ok,
              "purge coverage should warm the hot fact before maintenance");
  assert_true(store->touch_experiences({"experience-purge-hot"}, now_millis - 100).ok,
              "purge coverage should warm the hot experience before maintenance");

  dasall::memory::MemoryMaintenanceWorker worker(*store, config);
  const auto report = worker.execute(dasall::memory::MaintenanceRequest{});

  assert_equal(1, report.facts_purged,
               "maintenance should purge exactly one cold fact by decay");
  assert_equal(1, report.experiences_purged,
               "maintenance should purge exactly one cold experience by decay");
  assert_equal(1,
               query_count(database_path,
                           "SELECT COUNT(*) FROM facts WHERE fact_id = 'fact-purge-hot'"),
               "maintenance should keep the hot fact after decay evaluation");
  assert_equal(0,
               query_count(database_path,
                           "SELECT COUNT(*) FROM facts WHERE fact_id = 'fact-purge-cold'"),
               "maintenance should remove the cold fact after decay evaluation");
  assert_equal(1,
               query_count(database_path,
                           "SELECT COUNT(*) FROM experiences WHERE experience_id = 'experience-purge-hot'"),
               "maintenance should keep the hot experience after decay evaluation");
  assert_equal(0,
               query_count(database_path,
                           "SELECT COUNT(*) FROM experiences WHERE experience_id = 'experience-purge-cold'"),
               "maintenance should remove the cold experience after decay evaluation");

  store->close();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_candidate_collector_prefers_hot_records_and_touches_access_metadata();
    test_maintenance_purges_old_cold_records_by_decay();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}