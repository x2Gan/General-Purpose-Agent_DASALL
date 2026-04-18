#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

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

}  // namespace

int main() {
  try {
    test_sqlite_memory_store_persists_session_turn_and_summary_roundtrip();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
