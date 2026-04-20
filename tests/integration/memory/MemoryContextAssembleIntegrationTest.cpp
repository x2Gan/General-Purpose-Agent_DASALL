#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for memory context assemble coverage
#endif

#include "IMemoryManager.h"
#include "context/ContextPacketGuards.h"
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

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected_fragment) {
  return std::any_of(values.begin(), values.end(),
                     [&expected_fragment](const std::string& value) {
                       return value.find(expected_fragment) != std::string::npos;
                     });
}

[[nodiscard]] bool snapshot_has_slot(
    const dasall::memory::WorkingMemorySnapshot& snapshot,
    const std::string& key,
    const std::string& expected_value) {
  return std::any_of(snapshot.slots.begin(), snapshot.slots.end(),
                     [&key, &expected_value](const dasall::memory::WorkingMemorySlot& slot) {
                       return slot.key == key && slot.value == expected_value;
                     });
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.context.compression_trigger_turns = 2;
  config.context.compression_trigger_ratio = 0.5;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_request(
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

void test_memory_context_assemble_integration_closes_the_manager_loop() {
  using dasall::tests::support::assert_true;

  const auto database_path =
      make_temp_database_path("dasall-memory-context-assemble-integration");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto manager = dasall::memory::create_memory_manager(config);
  const auto init_code = manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "sqlite-backed memory manager should initialize for context assemble integration");

  const auto first_result = manager->write_back(make_request(
      "session-026-context", "turn-026-context-001", "记录 memory gate 状态",
      "已完成第一条 sqlite writeback", "第一版 context summary",
      "memory smoke gate 已初始化", 82));
  assert_true(!first_result.result_code.has_value(),
              "first writeback should seed the sqlite session for context assemble integration");

  const auto second_result = manager->write_back(make_request(
      "session-026-context", "turn-026-context-002", "继续收口 context assemble",
      "已完成第二条 sqlite writeback 并准备 assemble", "第二版 context summary",
      "context assemble 已具备 sqlite 主链", 93));
  assert_true(!second_result.result_code.has_value(),
              "second writeback should remain successful before context assembly");
  assert_true(second_result.summary_id.has_value(),
              "second writeback should materialize a latest summary id for export continuity");

  const auto export_result = manager->export_working_memory_snapshot(
      dasall::memory::WorkingMemoryExportRequest{
          .session_id = "session-026-context",
          .export_reason = "context-assemble-gate",
          .include_ephemeral_facts = true,
      });
  assert_true(!export_result.result_code.has_value(),
              "working-memory export should succeed before context assembly");
  assert_true(snapshot_has_slot(export_result.snapshot, "latest_turn_id",
                                "turn-026-context-002"),
              "working-memory export should preserve the latest turn id across writeback and assemble");
  assert_true(snapshot_has_slot(export_result.snapshot, "latest_summary_id",
                                *second_result.summary_id),
              "working-memory export should preserve the latest summary id across writeback and assemble");

  const auto context_result = manager->prepare_context(
      dasall::memory::MemoryContextRequest{
          .request_id = "req-026-context-001",
          .session_id = "session-026-context",
          .stage = "reasoning",
          .goal_summary = "验证 MemoryManager context assemble 最小闭环",
          .constraints_summary = "必须保留 goal 和 latest observation",
          .latest_observation_digest_summary = "最新观测表明 memory sqlite 主链已经接线",
          .visible_tools = {"shell", "cmake", "search"},
          .token_budget_hint = 220,
          .latency_budget_ms = 100,
          .external_evidence = {"external evidence: desktop_full gate"},
      });

  assert_true(!context_result.result_code.has_value(),
              "context assembly should succeed on the sqlite-backed manager path");
  assert_true(!context_result.degraded,
              "context assembly should stay non-degraded on the normal sqlite-backed path");
  assert_true(context_result.context_packet.request_id ==
                  std::optional<std::string>{"req-026-context-001"},
              "context assembly should preserve the request id on the manager path");
  assert_true(context_result.context_packet.current_goal_summary ==
                  std::optional<std::string>{"验证 MemoryManager context assemble 最小闭环"},
              "context assembly should preserve the goal summary under the sqlite-backed gate");
  assert_true(context_result.context_packet.latest_observation_digest_summary ==
                  std::optional<std::string>{"最新观测表明 memory sqlite 主链已经接线"},
              "context assembly should preserve latest observation under the sqlite-backed gate");
  assert_true(context_result.context_packet.summary_memory.has_value(),
              "context assembly should project summary memory once compression has run");
  assert_true(context_result.context_packet.belief_state_summary.has_value() &&
                  context_result.context_packet.belief_state_summary->find(
                      "context assemble 已具备 sqlite 主链") != std::string::npos,
              "context assembly should project persisted facts into belief_state_summary");
  assert_true(context_result.context_packet.active_tools.has_value() &&
                  context_result.context_packet.active_tools->size() == 3U,
              "context assembly should project visible tools into active_tools");
      assert_true(context_result.context_packet.policy_digest ==
              std::optional<std::string>{"必须保留 goal 和 latest observation"},
            "context assembly should preserve policy_digest on the sqlite-backed manager path");
  assert_true(context_result.context_packet.token_budget_report.has_value() &&
                  context_result.context_packet.token_budget_report->find(
                      "current_goal_summary") != std::string::npos,
              "context assembly should emit a structured token budget report");
  assert_true(contains_value(context_result.compression_notes, "strategy:template"),
              "context assembly should surface template compression notes when compression is triggered");
  assert_true(dasall::contracts::validate_context_packet_field_rules(
                  context_result.context_packet)
                  .ok,
              "context assembly should emit a ContextPacket that satisfies the frozen contract guards");

  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_context_assemble_integration_closes_the_manager_loop();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}