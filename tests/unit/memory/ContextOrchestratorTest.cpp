#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "config/MemoryConfig.h"
#include "context/ContextOrchestrator.h"
#include "context/ContextPacketGuards.h"
#include "memory/MemoryFact.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"

namespace {

class StubVectorMemoryIndexAdapter final : public dasall::memory::VectorMemoryIndexAdapter {
 public:
  bool available = true;
  std::vector<dasall::memory::VectorHit> hits;

  [[nodiscard]] bool is_available() const override {
    return available;
  }

  [[nodiscard]] dasall::memory::StoreResult upsert(
      const dasall::memory::VectorDocument& doc) override {
    return dasall::memory::StoreResult::success(doc.doc_id);
  }

  [[nodiscard]] std::vector<dasall::memory::VectorHit> search(
      const std::string& query_text, int top_k) const override {
    last_query = query_text;
    last_top_k = top_k;
    return hits;
  }

  [[nodiscard]] dasall::memory::VectorIndexHealth health() const override {
    return dasall::memory::VectorIndexHealth{
        .available = available,
        .indexed_doc_count = static_cast<int>(hits.size()),
        .last_rebuild_at = 0,
        .backend_type = "sqlite-vss",
    };
  }

  [[nodiscard]] dasall::memory::StoreResult rebuild_index() override {
    return dasall::memory::StoreResult::success("rebuild");
  }

  mutable std::string last_query;
  mutable int last_top_k = 0;
};

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
                                  const std::string& agent_response,
                                  std::vector<std::string> tool_refs = {},
                                  std::vector<std::string> observation_refs = {}) {
  dasall::contracts::Turn turn;
  turn.turn_id = turn_id;
  turn.session_id = session_id;
  turn.user_input = user_input;
  turn.agent_response = agent_response;
  if (!tool_refs.empty()) {
    turn.tool_call_refs = std::move(tool_refs);
  }
  if (!observation_refs.empty()) {
    turn.observation_refs = std::move(observation_refs);
  }
  turn.created_at = 1000;
  return turn;
}

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.user_id = "user-019";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 900;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed orchestrator session");
  }
}

void append_turn(dasall::tests::mocks::FakeMemoryStore& store,
                 const dasall::contracts::Turn& turn) {
  if (!store.append_turn(turn).ok) {
    throw std::runtime_error("failed to append orchestrator turn");
  }
}

void insert_fact(dasall::tests::mocks::FakeMemoryStore& store,
                 const std::string& session_id,
                 const std::string& fact_id,
                 const std::string& fact_text,
                 std::uint32_t confidence) {
  dasall::contracts::MemoryFact fact;
  fact.fact_id = fact_id;
  fact.session_id = session_id;
  fact.fact_text = fact_text;
  fact.source_turn_ids = std::vector<std::string>{"turn-019-002"};
  fact.confidence_score = confidence;
  fact.created_at = 1000;
  fact.fact_type = "constraint";
  if (!store.insert_fact(fact).ok) {
    throw std::runtime_error("failed to insert orchestrator fact");
  }
}

void upsert_existing_summary(dasall::tests::mocks::FakeMemoryStore& store,
                             const std::string& session_id) {
  dasall::contracts::SummaryMemory summary;
  summary.summary_id = "summary-019-existing";
  summary.session_id = session_id;
  summary.summary_text = "旧摘要";
  summary.source_turn_ids = std::vector<std::string>{"turn-019-000"};
  summary.decisions_made = std::vector<std::string>{"既有决策"};
  summary.confirmed_facts = std::vector<std::string>{"既有事实"};
  summary.tool_outcomes = std::vector<std::string>{"旧工具结果"};
  summary.created_at = 950;
  if (!store.upsert_summary(summary).ok) {
    throw std::runtime_error("failed to upsert existing summary");
  }
}

void test_context_orchestrator_maps_context_packet_slots_and_triggers_compression() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-019");
  append_turn(store, make_turn("turn-019-001", "session-019", "用户要求排查日志。",
                               "决定先检查 sqlite 日志。"));
  append_turn(store, make_turn("turn-019-002", "session-019", "确认 busy 报警已经定位。",
                               "已完成 store 检查，计划继续压缩上下文。"));
  append_turn(store, make_turn("turn-019-003", "session-019",
                               "请继续收口 ContextOrchestrator。",
                               "工具执行成功，下一步实现 slot 映射。",
                               {"tool-ctx-1"}, {"obs-ctx-1"}));
  upsert_existing_summary(store, "session-019");
  insert_fact(store, "session-019", "fact-019-001", "sqlite busy 已解除", 95);

  auto working_board = dasall::memory::create_working_memory_board();
  StubVectorMemoryIndexAdapter vector_index;
  vector_index.hits = {dasall::memory::VectorHit{
      .doc_id = "doc-019-001",
      .doc_type = "evidence",
      .score = 0.95F,
      .text_snippet = "向量检索命中：压缩路径需要 fallback。",
  }};

  dasall::memory::MemoryConfig config;
  config.vector.enabled = true;
  config.vector.search_top_k = 2;
  config.context.compression_trigger_turns = 2;
  config.context.compression_trigger_ratio = 0.5;

  auto collector = std::make_unique<dasall::memory::CandidateCollector>(
      *working_board, store, config, &vector_index);
  auto allocator = std::make_unique<dasall::memory::BudgetAllocator>(config);
  auto compressor = std::make_unique<dasall::memory::CompressionCoordinator>(store);
  dasall::memory::ContextOrchestrator orchestrator(
      std::move(collector), std::move(allocator), std::move(compressor), config);

  const auto result = orchestrator.assemble(dasall::memory::MemoryContextRequest{
      .request_id = "req-019-001",
      .session_id = "session-019",
      .stage = "reasoning",
      .goal_summary = "完成 ContextPacket 槽位映射",
      .constraints_summary = "必须保留 goal 和 latest observation",
      .latest_observation_digest_summary = "最近 observation 指向 sqlite busy 已解除",
      .visible_tools = {"shell", "search", "cmake"},
      .token_budget_hint = 220,
      .latency_budget_ms = 100,
      .external_evidence = {"external evidence: context gate"},
  });

  assert_true(!result.result_code.has_value(),
              "context orchestrator should succeed on the normal assembly path");
  assert_true(!result.degraded,
              "context orchestrator should stay non-degraded when all dependencies succeed");
  assert_true(result.context_packet.user_turn ==
                  std::optional<std::string>{"请继续收口 ContextOrchestrator。"},
              "context orchestrator should project the latest user turn into the user_turn slot");
  assert_true(result.context_packet.current_goal_summary ==
                  std::optional<std::string>{"完成 ContextPacket 槽位映射"},
              "context orchestrator should project the goal summary into current_goal_summary");
  assert_true(result.context_packet.summary_memory.has_value(),
              "context orchestrator should project summary_memory once compression is triggered");
    assert_true(
      (result.context_packet.retrieval_evidence.has_value() &&
       !result.context_packet.retrieval_evidence->empty() &&
       (contains_value(*result.context_packet.retrieval_evidence, "doc-019-001") ||
      contains_value(*result.context_packet.retrieval_evidence,
               "external evidence: context gate"))) ||
        contains_value(result.dropped_sections, "retrieval_evidence"),
      "context orchestrator should either retain retrieval_evidence in the packet or record that budget trim dropped it");
  assert_true(result.context_packet.active_tools.has_value() &&
                  result.context_packet.active_tools->size() == 3U,
              "context orchestrator should project visible tools into active_tools");
  assert_true(result.context_packet.policy_digest ==
                  std::optional<std::string>{"必须保留 goal 和 latest observation"},
              "context orchestrator should project constraints_summary into policy_digest");
  assert_true(result.context_packet.belief_state_summary.has_value() &&
                  result.context_packet.belief_state_summary->find("sqlite busy 已解除") !=
                      std::string::npos,
              "context orchestrator should project relevant facts into belief_state_summary");
  assert_true(result.context_packet.token_budget_report.has_value() &&
                  result.context_packet.token_budget_report->find("current_goal_summary") !=
                      std::string::npos,
              "context orchestrator should serialize the budget plan into token_budget_report");
  assert_true(contains_value(result.compression_notes, "strategy:template"),
              "context orchestrator should surface compression notes when compression is triggered");
  assert_true(dasall::contracts::validate_context_packet_field_rules(
                  result.context_packet)
                  .ok,
              "context orchestrator should emit a ContextPacket that satisfies the frozen contract guards");
  assert_true(vector_index.last_query == "完成 ContextPacket 槽位映射",
              "candidate collection should use the goal summary as the vector query text");
}

}  // namespace

int main() {
  try {
    test_context_orchestrator_maps_context_packet_slots_and_triggers_compression();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}