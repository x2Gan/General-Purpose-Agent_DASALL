#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "FakeMemoryStore.h"
#include "config/MemoryConfig.h"
#include "context/ContextOrchestrator.h"
#include "context/ContextPacketGuards.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

bool contains_value(const std::vector<std::string>& values,
                    const std::string& expected_fragment) {
  return std::any_of(values.begin(), values.end(),
                     [&expected_fragment](const std::string& value) {
                       return value.find(expected_fragment) != std::string::npos;
                     });
}

dasall::contracts::Turn make_turn(const std::string& turn_id,
                                  const std::string& session_id,
                                  const std::string& user_input,
                                  const std::string& agent_response) {
  dasall::contracts::Turn turn;
  turn.turn_id = turn_id;
  turn.session_id = session_id;
  turn.user_input = user_input;
  turn.agent_response = agent_response;
  turn.created_at = 1000;
  return turn;
}

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 900;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed degraded orchestrator session");
  }
}

void test_context_orchestrator_degrades_when_user_turn_must_fall_back_to_goal_summary() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  auto working_board = dasall::memory::create_working_memory_board();

  dasall::memory::MemoryConfig config;
  auto collector = std::make_unique<dasall::memory::CandidateCollector>(
      *working_board, store, config);
  auto allocator = std::make_unique<dasall::memory::BudgetAllocator>(config);
  auto compressor = std::make_unique<dasall::memory::CompressionCoordinator>(store);
  dasall::memory::ContextOrchestrator orchestrator(
      std::move(collector), std::move(allocator), std::move(compressor), config);

  const auto result = orchestrator.assemble(dasall::memory::MemoryContextRequest{
      .request_id = "req-019-degraded-001",
      .session_id = "session-019-degraded",
      .stage = "reasoning",
      .goal_summary = "恢复上下文",
      .constraints_summary = "必须输出有效 ContextPacket",
      .latest_observation_digest_summary = "尚无 observation",
      .visible_tools = {"shell"},
      .token_budget_hint = 128,
            .latency_budget_ms = 0,
            .external_evidence = {},
  });

  assert_true(result.degraded,
              "context orchestrator should report degraded when it must fall back to goal_summary for user_turn");
  assert_true(contains_value(result.warnings, "user_turn_fallback_goal_summary"),
              "context orchestrator should record that it fell back to goal_summary for user_turn");
  assert_true(result.context_packet.user_turn == std::optional<std::string>{"恢复上下文"},
              "context orchestrator should fall back to goal_summary when no persisted user turn is available");
  assert_true(result.context_packet.recent_history.has_value() &&
                  result.context_packet.recent_history->empty(),
              "context orchestrator should still provide an empty recent_history vector on degraded first-turn assembly");
  assert_true(dasall::contracts::validate_context_packet_field_rules(
                  result.context_packet)
                  .ok,
              "context orchestrator should still emit a contract-valid ContextPacket on degraded fallback");
}

void test_context_orchestrator_warns_when_compression_is_needed_but_unavailable() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-019-no-compressor");
  if (!store.append_turn(make_turn("turn-019-d-001", "session-019-no-compressor",
                                   "请整理历史。", "决定先压缩历史。"))
           .ok ||
      !store.append_turn(make_turn("turn-019-d-002", "session-019-no-compressor",
                                   "确认需要最新 observation。",
                                   "计划继续收口 ContextOrchestrator。"))
           .ok) {
    throw std::runtime_error("failed to append degraded compression turns");
  }

  auto working_board = dasall::memory::create_working_memory_board();

  dasall::memory::MemoryConfig config;
  config.context.compression_trigger_turns = 1;
  config.context.compression_trigger_ratio = 0.2;
  auto collector = std::make_unique<dasall::memory::CandidateCollector>(
      *working_board, store, config);
  auto allocator = std::make_unique<dasall::memory::BudgetAllocator>(config);
  dasall::memory::ContextOrchestrator orchestrator(
      std::move(collector), std::move(allocator), nullptr, config);

  const auto result = orchestrator.assemble(dasall::memory::MemoryContextRequest{
      .request_id = "req-019-degraded-002",
      .session_id = "session-019-no-compressor",
      .stage = "reasoning",
      .goal_summary = "保持上下文可用",
      .constraints_summary = "必须优先保留最新 observation",
      .latest_observation_digest_summary = "observation: compression skipped",
        .visible_tools = {},
      .token_budget_hint = 64,
        .latency_budget_ms = 0,
        .external_evidence = {},
  });

  assert_true(result.degraded,
              "context orchestrator should mark the assembly as degraded when compression is needed but unavailable");
  assert_true(contains_value(result.warnings, "compression_skipped"),
              "context orchestrator should emit compression_skipped when the compressor is missing");
  assert_true(dasall::contracts::validate_context_packet_field_rules(
                  result.context_packet)
                  .ok,
              "context orchestrator should still emit a contract-valid ContextPacket when compression is skipped");
}

}  // namespace

int main() {
  try {
    test_context_orchestrator_degrades_when_user_turn_must_fall_back_to_goal_summary();
    test_context_orchestrator_warns_when_compression_is_needed_but_unavailable();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}