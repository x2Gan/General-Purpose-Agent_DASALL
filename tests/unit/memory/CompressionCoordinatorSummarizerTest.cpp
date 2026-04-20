#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "FakeMemoryStore.h"
#include "ISummarizer.h"
#include "memory/Session.h"
#include "support/TestAssertions.h"
#include "writeback/CompressionCoordinator.h"

namespace {

class MockSummarizer final : public dasall::memory::ISummarizer {
 public:
  bool throw_on_call = false;
  dasall::memory::SummaryGenerationResult result;

  [[nodiscard]] dasall::memory::SummaryGenerationResult summarize(
      const dasall::memory::SummaryGenerationRequest& request) override {
    last_session_id = request.session_id;
    ++call_count;
    if (throw_on_call) {
      throw std::runtime_error("summarizer failure");
    }
    return result;
  }

  int call_count = 0;
  std::string last_session_id;
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
    throw std::runtime_error("failed to seed summarizer session");
  }
}

void test_compression_coordinator_uses_summarizer_projection_when_available() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  seed_session(store, "session-018-summarizer");

  MockSummarizer summarizer;
  summarizer.result.projection.summary_text = "LLM summary";
  summarizer.result.projection.decisions_made = {"LLM 决策"};
  summarizer.result.projection.confirmed_facts = {"LLM 事实"};
  summarizer.result.projection.tool_outcomes = {"LLM outcome"};
  summarizer.result.projection.source_turn_ids = {"turn-018-s-001"};
  summarizer.result.projection.estimated_tokens = 42;
  summarizer.result.warnings = {"llm_summary_used"};

  dasall::memory::CompressionCoordinator coordinator(store, &summarizer);
  const auto output = coordinator.compress(dasall::memory::CompressionInput{
      .session_id = "session-018-summarizer",
      .source_turns = {
          make_turn("turn-018-s-001", "session-018-summarizer",
                    "总结本轮工作。", "由 summarizer 直接产出摘要。"),
      },
      .existing_summary = std::nullopt,
      .target_token_budget = 0,
      .materialize_latest_summary = true,
      .strategy_hint = "llm",
  });

  assert_equal(1, summarizer.call_count,
               "compression coordinator should invoke the summarizer once when it is available");
  assert_equal("session-018-summarizer", summarizer.last_session_id,
               "compression coordinator should forward the session id to the summarizer");
  assert_true(output.projection.summary_text == "LLM summary",
              "compression coordinator should use the summarizer projection when it is valid");
  assert_true(contains_value(output.compression_notes, "llm_summary_used"),
              "compression coordinator should preserve summarizer warnings as compression notes");
  assert_true(contains_value(output.compression_notes, "strategy:summarizer"),
              "compression coordinator should mark the summarizer strategy when fallback is not used");

  const auto latest_summary = store.load_latest_summary("session-018-summarizer");
  assert_true(latest_summary.has_value(),
              "summarizer projection should still be materialized through the store surface");
  assert_true(latest_summary->summary_text == std::optional<std::string>{"LLM summary"},
              "materialized summary should preserve the summarizer text");
}

void test_compression_coordinator_falls_back_to_template_when_summarizer_throws() {
  using dasall::tests::support::assert_true;

  dasall::tests::mocks::FakeMemoryStore store;
  MockSummarizer summarizer;
  summarizer.throw_on_call = true;

  dasall::memory::CompressionCoordinator coordinator(store, &summarizer);
  const auto output = coordinator.compress(dasall::memory::CompressionInput{
      .session_id = "session-018-fallback",
      .source_turns = {
          make_turn("turn-018-f-001", "session-018-fallback",
                    "确认 summary supporting objects 已冻结。",
                    "决定先收口压缩，再进入 ContextOrchestrator。"),
      },
      .existing_summary = std::nullopt,
      .target_token_budget = 0,
      .materialize_latest_summary = false,
      .strategy_hint = "llm",
  });

  assert_true(output.compression_applied,
              "fallback compression should still produce a summary when the summarizer throws");
  assert_true(contains_value(output.compression_notes, "summarizer_fallback"),
              "compression coordinator should record a fallback note when summarizer invocation fails");
  assert_true(contains_value(output.compression_notes, "strategy:template"),
              "compression coordinator should switch back to the template strategy after summarizer failure");
  assert_true(!output.projection.summary_text.empty(),
              "fallback compression should still produce template summary text");
  assert_true(contains_value(output.projection.decisions_made, "决定先收口压缩"),
              "fallback compression should extract Chinese decision text from the turn response");
}

}  // namespace

int main() {
  try {
    test_compression_coordinator_uses_summarizer_projection_when_available();
    test_compression_coordinator_falls_back_to_template_when_summarizer_throws();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}