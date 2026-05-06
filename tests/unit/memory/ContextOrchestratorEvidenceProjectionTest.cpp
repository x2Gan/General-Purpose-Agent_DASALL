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

void seed_session(dasall::tests::mocks::FakeMemoryStore& store,
                  const std::string& session_id) {
  dasall::contracts::Session session;
  session.session_id = session_id;
  session.user_id = "user-009";
  session.turn_ids = std::vector<std::string>{};
  session.created_at = 900;
  if (!store.create_session(session).ok) {
    throw std::runtime_error("failed to seed session for evidence projection test");
  }
}

void append_turn(dasall::tests::mocks::FakeMemoryStore& store,
                 const std::string& session_id) {
  dasall::contracts::Turn turn;
  turn.turn_id = "turn-009-001";
  turn.session_id = session_id;
  turn.user_input = "请保留 structured evidence refs。";
  turn.agent_response = "已进入 memory context evidence projection 路径。";
  turn.created_at = 1000;
  if (!store.append_turn(turn).ok) {
    throw std::runtime_error("failed to append turn for evidence projection test");
  }
}

void test_context_orchestrator_preserves_parallel_evidence_views() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-009");
  append_turn(store, "session-009");

  auto working_board = dasall::memory::create_working_memory_board();

  dasall::memory::MemoryConfig config;
  config.vector.enabled = false;
  config.context.compression_trigger_turns = 8;
  config.context.compression_trigger_ratio = 0.95;

  auto collector = std::make_unique<dasall::memory::CandidateCollector>(
      *working_board, store, store, store, store, config);
  auto allocator = std::make_unique<dasall::memory::BudgetAllocator>(config);
  auto compressor = std::make_unique<dasall::memory::CompressionCoordinator>(store);
  dasall::memory::ContextOrchestrator orchestrator(
      std::move(collector), std::move(allocator), std::move(compressor), config);

  const dasall::contracts::RetrievalEvidenceRef evidence_ref{
      .evidence_ref = "evidence-009-001",
      .source_ref = "knowledge-doc-009",
      .source_kind = "knowledge_chunk",
      .summary_text = "保留 sqlite busy 解除的结构化证据引用。",
      .trust_level = "high",
      .freshness = "fresh",
      .anchor_locator = std::string{"chunk:3"},
  };

  const auto result = orchestrator.assemble(dasall::memory::MemoryContextRequest{
      .request_id = "req-009-001",
      .session_id = "session-009",
      .stage = "reasoning",
      .goal_summary = "保持 memory evidence 双轨投影",
      .constraints_summary = "保留文本 evidence 与 structured refs",
      .latest_observation_digest_summary = "latest observation: sqlite busy 已解除",
      .visible_tools = {"shell", "search"},
      .token_budget_hint = 512,
      .latency_budget_ms = 80,
      .external_evidence = {"external evidence: sqlite busy 已解除"},
      .retrieval_evidence_refs = {evidence_ref},
  });

  assert_true(!result.result_code.has_value(),
              "context orchestrator evidence projection should stay on the success path");
  assert_true(!result.degraded,
              "context orchestrator evidence projection should not degrade on the nominal path");
  assert_true(result.context_packet.retrieval_evidence.has_value(),
              "context orchestrator should preserve the textual evidence view");
  assert_equal(1,
               static_cast<int>(result.context_packet.retrieval_evidence->size()),
               "context orchestrator should keep exactly the request-provided textual evidence when vector retrieval is disabled");
  assert_equal("external evidence: sqlite busy 已解除",
               result.context_packet.retrieval_evidence->front(),
               "context orchestrator should keep the old textual evidence path intact");

  assert_true(result.context_packet.retrieval_evidence_refs.has_value(),
              "context orchestrator should project structured evidence refs into ContextPacket");
  assert_equal(1,
               static_cast<int>(result.context_packet.retrieval_evidence_refs->size()),
               "context orchestrator should preserve the full structured evidence ref set");

  const auto& projected_ref = result.context_packet.retrieval_evidence_refs->front();
  assert_equal(evidence_ref.evidence_ref,
               projected_ref.evidence_ref,
               "context orchestrator should preserve evidence_ref across the memory->contracts seam");
  assert_equal(evidence_ref.source_ref,
               projected_ref.source_ref,
               "context orchestrator should preserve source_ref across the memory->contracts seam");
  assert_equal(evidence_ref.summary_text,
               projected_ref.summary_text,
               "context orchestrator should preserve summary_text across the memory->contracts seam");
  assert_equal(evidence_ref.freshness,
               projected_ref.freshness,
               "context orchestrator should preserve freshness across the memory->contracts seam");
  assert_true(dasall::contracts::validate_context_packet_field_rules(result.context_packet).ok,
              "context orchestrator evidence projection should satisfy ContextPacket field guards");
}

}  // namespace

int main() {
  try {
    test_context_orchestrator_preserves_parallel_evidence_views();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}