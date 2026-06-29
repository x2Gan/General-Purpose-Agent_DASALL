#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cross-session fact query coverage
#endif

#include "context/BudgetAllocator.h"
#include "context/CandidateCollector.h"
#include "context/ContextOrchestrator.h"
#include "context/ContextPacketGuards.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"
#include "working/IWorkingMemoryBoard.h"
#include "writeback/CompressionCoordinator.h"

namespace {

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-memory-cross-session-facts-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

[[nodiscard]] bool contains_fragment(const std::string& text,
                                     const std::string& fragment) {
  return text.find(fragment) != std::string::npos;
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.fact_confidence_floor = 80;
  config.context.compression_trigger_turns = 8;
  config.context.compression_trigger_ratio = 0.8;
  config.vector.enabled = false;
  return config;
}

dasall::contracts::Session make_session(const std::string& session_id,
                                        const std::string& user_id,
                                        std::int64_t created_at) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.user_id = user_id;
  session.turn_ids = std::vector<std::string>{};
  session.created_at = created_at;
  session.last_active_at = created_at;
  return session;
}

dasall::contracts::Turn make_turn(const std::string& turn_id,
                                  const std::string& session_id,
                                  const std::string& user_input,
                                  const std::string& agent_response,
                                  std::int64_t created_at) {
  dasall::contracts::Turn turn;
  turn.turn_id = turn_id;
  turn.session_id = session_id;
  turn.user_input = user_input;
  turn.agent_response = agent_response;
  turn.created_at = created_at;
  return turn;
}

dasall::contracts::MemoryFact make_fact(const std::string& fact_id,
                                        const std::string& session_id,
                                        const std::string& turn_id,
                                        const std::string& fact_text,
                                        std::uint32_t confidence_score,
                                        std::int64_t created_at) {
  dasall::contracts::MemoryFact fact;
  fact.fact_id = fact_id;
  fact.session_id = session_id;
  fact.fact_text = fact_text;
  fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact.confidence_score = confidence_score;
  fact.created_at = created_at;
  fact.fact_type = "preference";
  return fact;
}

void test_memory_cross_session_fact_query_projects_user_level_facts() {
  using dasall::tests::support::assert_true;

  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  assert_true(!store->open(config).has_value(),
              "sqlite store should open for cross-session fact query integration coverage");

  const auto historical_session = make_session("session-cross-001", "user-cross-001", 1000);
  const auto active_session = make_session("session-cross-002", "user-cross-001", 2000);
  assert_true(store->create_session(historical_session).ok,
              "sqlite store should create the historical session for the shared user");
  assert_true(store->create_session(active_session).ok,
              "sqlite store should create the active session for the shared user");

  const auto historical_turn = make_turn(
      "turn-cross-001", "session-cross-001", "记住我的桌面 profile 偏好",
      "已记录 desktop_full 偏好", 1010);
  const auto active_turn = make_turn(
      "turn-cross-002", "session-cross-002", "继续当前会话",
      "准备装配跨 session 的用户偏好", 2010);
  assert_true(store->append_turn(historical_turn).ok,
              "sqlite store should append a historical turn before inserting the shared fact");
  assert_true(store->append_turn(active_turn).ok,
              "sqlite store should append an active-session turn for context assembly");

  const auto historical_fact = make_fact(
      "fact-cross-001", "session-cross-001", "turn-cross-001",
      "用户偏好 desktop_full profile。", 95, 1020);
  assert_true(store->insert_fact(historical_fact).ok,
              "sqlite store should persist the historical fact for user-scoped lookup");

  auto working_board = dasall::memory::create_working_memory_board();
  auto collector = std::make_unique<dasall::memory::CandidateCollector>(
      *working_board, *store, config);
  auto allocator = std::make_unique<dasall::memory::BudgetAllocator>(config);
  auto compressor = std::make_unique<dasall::memory::CompressionCoordinator>(*store);
  dasall::memory::ContextOrchestrator orchestrator(
      std::move(collector), std::move(allocator), std::move(compressor), config);

  const auto result = orchestrator.assemble(dasall::memory::MemoryContextRequest{
      .request_id = "req-cross-001",
      .session_id = "session-cross-002",
      .trace_id = "trace-cross-001",
      .stage = "reasoning",
      .user_turn = "继续当前会话",
      .goal_summary = "恢复同一用户的历史偏好",
      .constraints_summary = "必须保留跨 session 的用户偏好",
      .latest_observation_digest_summary = "当前 session 尚未再次声明这条偏好",
      .visible_tools = {"search"},
      .token_budget_hint = 512,
      .latency_budget_ms = 100,
      .external_evidence = {},
      .retrieval_evidence_refs = {},
  });

  assert_true(!result.result_code.has_value(),
              "cross-session fact query integration should assemble context successfully");
  assert_true(!result.degraded,
              "cross-session fact query integration should stay non-degraded on the happy path");
  assert_true(result.context_packet.belief_state_summary.has_value(),
              "user-level cross-session facts should materialize belief_state_summary for the active session");
  assert_true(contains_fragment(*result.context_packet.belief_state_summary,
                                "用户偏好 desktop_full profile。"),
              "belief_state_summary should contain the fact that was only written in the sibling session");
  assert_true(dasall::contracts::validate_context_packet_field_rules(
                  result.context_packet)
                  .ok,
              "cross-session fact query integration should keep the emitted ContextPacket contract-valid");

  store->close();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_cross_session_fact_query_projects_user_level_facts();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}