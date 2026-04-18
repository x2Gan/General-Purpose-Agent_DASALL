#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <sqlite3.h>

#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-store-" + std::to_string(timestamp) + ".db");
}

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int query_scalar_count(const std::filesystem::path& database_path,
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

dasall::memory::MemoryConfig make_sqlite_config(const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = "sqlite";
  config.storage.db_path = database_path.string();
  config.storage.reader_pool_size = 2;
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  return config;
}

void test_sqlite_memory_store_persists_session_turn_and_summary_roundtrip() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_sqlite_config(database_path);

  assert_true(!store->open(config).has_value(),
              "sqlite store should open against a temp database");

  dasall::contracts::Session session;
  session.session_id = "session-014";
  session.turn_ids = std::vector<std::string>{};
  session.user_id = "user-014";
  session.metadata_digest = "metadata-v1";
  session.created_at = 1000;
  session.last_active_at = 1000;
  session.tags = std::vector<std::string>{"alpha", "beta"};

  const auto session_result = store->create_session(session);
  assert_true(session_result.ok, "sqlite store should persist a new session row");

  dasall::contracts::Turn first_turn;
  first_turn.turn_id = "turn-001";
  first_turn.session_id = "session-014";
  first_turn.user_input = "first question";
  first_turn.agent_response = "first answer";
  first_turn.tool_call_refs = std::vector<std::string>{"tool-1"};
  first_turn.observation_refs = std::vector<std::string>{"obs-1"};
  first_turn.created_at = 1010;
  first_turn.tags = std::vector<std::string>{"first"};

  dasall::contracts::Turn second_turn;
  second_turn.turn_id = "turn-002";
  second_turn.session_id = "session-014";
  second_turn.user_input = "second question";
  second_turn.agent_response = "second answer";
  second_turn.tool_call_refs = std::vector<std::string>{"tool-2"};
  second_turn.observation_refs = std::vector<std::string>{"obs-2"};
  second_turn.summary_memory_ref = "summary-001";
  second_turn.created_at = 1020;
  second_turn.tags = std::vector<std::string>{"second"};

  assert_true(store->append_turn(first_turn).ok,
              "sqlite store should append the first turn");
  assert_true(store->append_turn(second_turn).ok,
              "sqlite store should append the second turn");
  assert_true(store->update_session_active("session-014", 1030).ok,
              "sqlite store should update the session activity timestamp");

  dasall::contracts::SummaryMemory summary;
  summary.summary_id = "summary-001";
  summary.session_id = "session-014";
  summary.summary_text = "condensed memory";
  summary.source_turn_ids = std::vector<std::string>{"turn-001", "turn-002"};
  summary.decisions_made = std::vector<std::string>{"ship sqlite baseline"};
  summary.confirmed_facts = std::vector<std::string>{"session persisted"};
  summary.tool_outcomes = std::vector<std::string>{"tool-2 ok"};
  summary.created_at = 1040;
  summary.tags = std::vector<std::string>{"summary"};

  assert_true(store->upsert_summary(summary).ok,
              "sqlite store should persist the latest summary");

  const auto bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = "session-014", .recent_turn_limit = 1});
  assert_true(bundle.session.session_id == std::optional<std::string>{"session-014"},
              "load_session_bundle should restore the session row");
  assert_true(bundle.session.latest_summary_memory_ref ==
                  std::optional<std::string>{"summary-001"},
              "load_session_bundle should reflect the latest summary pointer");
  assert_true(bundle.session.turn_ids.has_value() && bundle.session.turn_ids->size() == 2U,
              "load_session_bundle should restore the ordered turn id index");
  assert_equal(2, bundle.total_turn_count,
               "load_session_bundle should surface the full persisted turn count");
  assert_equal(1, static_cast<int>(bundle.recent_turns.size()),
               "recent_turn_limit should cap the returned turns");
  assert_true(bundle.recent_turns.front().turn_id == std::optional<std::string>{"turn-002"},
              "recent turns should be returned newest-first");
  assert_true(bundle.recent_turns.front().summary_memory_ref ==
                  std::optional<std::string>{"summary-001"},
              "latest turn mapping should preserve summary anchors");

  const auto loaded_summary = store->load_latest_summary("session-014");
  assert_true(loaded_summary.has_value(),
              "load_latest_summary should return the persisted summary row");
  assert_true(loaded_summary->summary_text == std::optional<std::string>{"condensed memory"},
              "load_latest_summary should preserve summary text");
  assert_true(loaded_summary->source_turn_ids.has_value() &&
                  loaded_summary->source_turn_ids->size() == 2U,
              "load_latest_summary should restore source turn ids");

  store->close();
  assert_true(!store->open(config).has_value(),
              "sqlite store should reopen an existing migrated database");

  const auto reopened_bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = "session-014", .recent_turn_limit = 2});
  assert_equal(2, reopened_bundle.total_turn_count,
               "reopened sqlite store should preserve persisted turns");
  assert_true(reopened_bundle.session.user_id == std::optional<std::string>{"user-014"},
              "reopened sqlite store should preserve session metadata");

  store->close();
  std::filesystem::remove(database_path);
}

void test_sqlite_memory_store_persists_fact_experience_and_maintenance_paths() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto config = make_sqlite_config(database_path);
  const auto now_millis = current_time_millis();

  assert_true(!store->open(config).has_value(),
              "sqlite store should open for fact/experience coverage");

  dasall::contracts::Session session;
  session.session_id = "session-015";
  session.turn_ids = std::vector<std::string>{};
  session.user_id = "user-015";
  session.created_at = now_millis - 1000;

  assert_true(store->create_session(session).ok,
              "sqlite store should create a session before fact/experience inserts");

  dasall::contracts::Turn turn;
  turn.turn_id = "turn-015-001";
  turn.session_id = "session-015";
  turn.user_input = "capture a fact";
  turn.created_at = now_millis - 900;
  assert_true(store->append_turn(turn).ok,
              "sqlite store should append a turn for count_turns coverage");

  dasall::contracts::MemoryFact primary_fact;
  primary_fact.fact_id = "fact-015-001";
  primary_fact.session_id = "session-015";
  primary_fact.fact_text = "用户要求每轮任务完成后立即推送远端。";
  primary_fact.source_turn_ids = std::vector<std::string>{"turn-015-001"};
  primary_fact.confidence_score = 95;
  primary_fact.created_at = now_millis - 800;
  primary_fact.fact_type = "preference";
  primary_fact.source_observation_refs = std::vector<std::string>{"obs-015-001"};
  primary_fact.valid_until = now_millis + 3600000;
  primary_fact.evidence_digest = "evidence-015-001";
  primary_fact.tags = std::vector<std::string>{"fact", "primary"};

  dasall::contracts::MemoryFact superseded_fact;
  superseded_fact.fact_id = "fact-015-002";
  superseded_fact.session_id = "session-015";
  superseded_fact.fact_text = "旧事实需要被 supersede。";
  superseded_fact.source_turn_ids = std::vector<std::string>{"turn-015-001"};
  superseded_fact.confidence_score = 70;
  superseded_fact.created_at = now_millis - 700;
  superseded_fact.fact_type = "constraint";

  assert_true(store->insert_fact(primary_fact).ok,
              "sqlite store should insert the primary fact");
  assert_true(store->insert_fact(superseded_fact).ok,
              "sqlite store should insert the fact that will be superseded");
  assert_true(store->supersede_fact("fact-015-002", "fact-015-003").ok,
              "sqlite store should record fact supersession");

  const auto filtered_facts = store->query_facts(dasall::memory::FactQuery{
      .session_id = std::optional<std::string>{"session-015"},
      .user_id = std::optional<std::string>{"user-015"},
      .fact_type = std::optional<std::string>{"preference"},
      .min_confidence = 80,
      .exclude_superseded = true,
      .limit = 10,
  });
  assert_equal(1, filtered_facts.total_count,
               "query_facts should filter by session/user/type/confidence and exclude superseded items");
  assert_true(filtered_facts.facts.front().fact_id ==
                  std::optional<std::string>{"fact-015-001"},
              "query_facts should return the surviving fact row");
  assert_true(filtered_facts.facts.front().source_observation_refs.has_value() &&
                  filtered_facts.facts.front().source_observation_refs->size() == 1U,
              "query_facts should restore observation refs from the sqlite sidecar field");
  assert_true(filtered_facts.facts.front().valid_until.has_value(),
              "query_facts should restore valid_until from the sqlite sidecar field");

  const auto all_facts = store->query_facts(dasall::memory::FactQuery{
      .session_id = std::optional<std::string>{"session-015"},
      .user_id = std::optional<std::string>{"user-015"},
      .fact_type = std::nullopt,
      .min_confidence = 0,
      .exclude_superseded = false,
      .limit = 10,
  });
  assert_equal(2, all_facts.total_count,
               "query_facts should surface superseded rows when exclude_superseded is false");

  dasall::contracts::ExperienceMemory durable_experience;
  durable_experience.experience_id = "exp-015-001";
  durable_experience.session_id = "session-015";
  durable_experience.lesson_summary = "在补充持久化路径前先对齐 contract 与 schema。";
  durable_experience.trigger_condition = "store_extension";
  durable_experience.recommended_action = "add_targeted_store_tests";
  durable_experience.created_at = now_millis - 600;
  durable_experience.source_fact_ids = std::vector<std::string>{"fact-015-001"};
  durable_experience.source_turn_ids = std::vector<std::string>{"turn-015-001"};
  durable_experience.effectiveness_score = 88;
  durable_experience.applicable_domains = std::vector<std::string>{"memory", "tooling"};
  durable_experience.risk_notes = "仅在 schema 已冻结时复用此路径。";
  durable_experience.expires_at = now_millis + 3600000;
  durable_experience.tags = std::vector<std::string>{"stage:plan", "experience"};

  dasall::contracts::ExperienceMemory expired_experience;
  expired_experience.experience_id = "exp-015-002";
  expired_experience.session_id = "session-015";
  expired_experience.lesson_summary = "这条经验已过期。";
  expired_experience.trigger_condition = "expired";
  expired_experience.recommended_action = "ignore";
  expired_experience.created_at = now_millis - 500;
  expired_experience.expires_at = now_millis - 1;
  expired_experience.tags = std::vector<std::string>{"stage:plan"};

  assert_true(store->insert_experience(durable_experience).ok,
              "sqlite store should insert the durable experience row");
  assert_true(store->insert_experience(expired_experience).ok,
              "sqlite store should insert the expired experience row");

  const auto filtered_experiences = store->query_experiences(dasall::memory::ExperienceQuery{
      .session_id = std::optional<std::string>{"session-015"},
      .user_id = std::optional<std::string>{"user-015"},
      .stage = std::optional<std::string>{"plan"},
      .applicable_domains = std::optional<std::vector<std::string>>{
          std::vector<std::string>{"tooling"}},
      .exclude_expired = true,
      .limit = 10,
  });
  assert_equal(1, filtered_experiences.total_count,
               "query_experiences should filter by session/user/stage/domain and exclude expired rows");
  assert_true(filtered_experiences.experiences.front().experience_id ==
                  std::optional<std::string>{"exp-015-001"},
              "query_experiences should return the durable experience row");
  assert_true(filtered_experiences.experiences.front().source_fact_ids.has_value() &&
                  filtered_experiences.experiences.front().source_fact_ids->size() == 1U,
              "query_experiences should restore source_fact_ids from the sqlite sidecar field");
  assert_true(filtered_experiences.experiences.front().risk_notes ==
                  std::optional<std::string>{"仅在 schema 已冻结时复用此路径。"},
              "query_experiences should restore risk_notes from the sqlite sidecar field");

  assert_equal(1, static_cast<int>(store->count_turns("session-015")),
               "count_turns should report the persisted turn count for the session");
  assert_true(store->quarantine_record("fact", "fact-015-002", "superseded_conflict").ok,
              "quarantine_record should persist a quarantine row");
  assert_equal(1, query_scalar_count(database_path, "SELECT COUNT(*) FROM quarantined_records"),
               "quarantine_record should write into the quarantine table");

  store->close();
  std::filesystem::remove(database_path);
}

}  // namespace

int main() {
  try {
    test_sqlite_memory_store_persists_session_turn_and_summary_roundtrip();
    test_sqlite_memory_store_persists_fact_experience_and_maintenance_paths();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
